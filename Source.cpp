#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <map>
#include <regex>
/*
Author: Zackary Finer
		011195639

Description:
	For this submission, i've made several modifications to the address space class, added a sharedData struct, and a DataLoader class.
	There is no paging simulated here, but data which is shared is (of course) only allocated once.

*/
enum DataType {
	T_FL,
	T_INT,
	T_CH,
	T_STR,
	T_BYTE,
	T_VOID//used to denote end of data or nullpointer
};
std::string toString(DataType c)
{
	switch (c)
	{
	case T_FL:
		return "FLOAT";
	case T_INT:
		return "INT";
	case T_CH:
		return "CHAR";
	case T_STR:
		return "STRING";
	case T_BYTE:
		return "BYTE";
	case T_VOID:
		return "VOID";
	}
}
struct data_entry {
	DataType dataType;
	void* data;
	//since this class is only responsible for holding the addresses, we will not be responsible for de-allocating them
	data_entry() {
		dataType = T_VOID;
		data = nullptr;
	}
	data_entry(void* _data, DataType _dtype) {
		data = _data;
		dataType = _dtype;
	}
	data_entry(const data_entry & other) {
		dataType = other.dataType;
		data = other.data;
	}
	void operator=(const data_entry & other)
	{
		dataType = other.dataType;
		data = other.data;
	}
	std::string toString()
	{
		if (data == nullptr)
		{
			return "NULL";
		}
		switch (dataType)
		{
		case T_CH:
			return std::string(1, *(char*)data);
		case T_FL:
			return std::to_string(*(float*)data);
		case T_INT:
			return std::to_string(*(int*)data);
		case T_STR:
			return *(std::string*)data;
		case T_BYTE:
		{
			std::stringstream stream;
			stream << "0x" << std::hex << (int)(*(char*)data);
			return stream.str();
		}
		default:
			return "NULL";
		}
	}
};

struct memsafe_data_entry {
	DataType dataType;
	void* data;
	memsafe_data_entry() {
		dataType = T_VOID;
		data = nullptr;
	}
	memsafe_data_entry(const data_entry & other) {
		dataType = other.dataType;
		data = other.data;
	}
	memsafe_data_entry(void* _data, DataType _dtype) {
		data = _data;
		dataType = _dtype;
	}
	
	memsafe_data_entry(const memsafe_data_entry & other) {
		dataType = other.dataType;
		data = other.data;
	}
	void operator=(const memsafe_data_entry & other)
	{
		delete data;//clear the old data
		dataType = other.dataType;
		data = other.data;
	}
	void operator=(const data_entry & other)
	{
		delete data;//clear the old data
		dataType = other.dataType;
		data = other.data;
	}
	std::string toString()
	{
		if (data == nullptr)
		{
			return "NULL";
		}
		switch (dataType)
		{
			case T_CH:
				return std::string(1, *(char*)data);
			case T_FL:
				return std::to_string(*(float*)data);
			case T_INT:
				return std::to_string(*(int*)data);
			case T_STR:
				return *(std::string*)data;
			case T_BYTE:
			{
				std::stringstream stream;
				stream << "0x" << std::hex << (int)(*(char*)data);
				return stream.str();
			}
			default:
				return "NULL";
		}
	}
	~memsafe_data_entry() {
		if (dataType == T_STR)
		{
			std::string* strD = static_cast<std::string*>(data);
			delete strD;//neccessary to cast to string type, in order to call appropriate destructor method
		}
		else {
			delete data;
		}
	}
};

#define DEFUALT_STACK_SIZE 256
#define DEFAULT_HEAP_SIZE 2048*8 // 16,384 or 2^14 or
//we will be inserting padding between the stack, dynamic, and data regions to compensate for any expansions of these regions that may occur during runtime
#define TEXT_START 0x1
#define BSS_START 0xffff
#define DATA_START 0x1fffe
#define DYNAMIC_START 0x3fffc
#define STACK_START 0x8001fffd

class MemStack {
	memsafe_data_entry* m_data;
	int m_head;
	int m_max_Size;
public:
	int getMax() { return m_max_Size; }
	MemStack(int size = DEFUALT_STACK_SIZE) {
		m_max_Size = size;
		m_data = new memsafe_data_entry[m_max_Size];
		m_head = -1;
	}
	memsafe_data_entry& operator[](int index) {
		if (index <= m_head)
		{
			return m_data[index];//i think data + index would also work, although this makes me uncomfortable
		}
		std::cerr << "ERROR, INDEX OUT OF BOUNDS OF STACK\n";
	}

	void push(const data_entry & entry) {
		if (m_head < m_max_Size - 1) {
			m_data[++m_head] = entry;
			return;
		}
		std::cerr << "ERROR, STACK OVERFLOW\n";
	}

	void pop() {
		if (m_head >= 0)
		{
			m_head--;
			return;
		}
		std::cerr << "ERROR, STACK UNDERFLOW\n";
	}
	memsafe_data_entry& peek() {
		if (m_head >= 0)
			return m_data[m_head];
		std::cerr << "ERROR, STACK UNDERFLOW\n";
	}
	~MemStack()
	{
		delete[] m_data;//when the stack is deleted, de-allocate everything within it
	}
};

class DynamicRegion {
	/*
	I decided to implement this region as a tree of nodes, connected by links.
	A doubly linked list of nodes at each level of the tree is maintained to allow quicker memory searches
	However, in the future I would hope to use a much simpler data structure.
	*/
	struct mem_block {
		mem_block *parent=nullptr, *prev = nullptr, *next = nullptr, *lChild = nullptr, *rChild = nullptr;
		bool free = true;
		int index;
		int size;
		mem_block() { index = 0; }
		mem_block(int index, int size) { this->index = index; this->size = size; }
		~mem_block() {
			if (lChild != nullptr) delete lChild;
			if (rChild != nullptr) delete rChild;
		}
	};
	mem_block** m_buddy_list;
	int m_buddy_list_size;
	char* m_dataRegion;
	int m_region_size;
	static inline int fastlog2(int val) {
		int lvl = 0;
		while (val >>= 1) lvl++;//bitshift by 1, , equivalent to val /= 2, until 0. This should return the number of times it can be divided by 2
		return lvl;
	}
	static inline int fastPow2(int val) {//returns 2 ^ val
		return 1 << val;
	}
	static inline int getP2(int val) {
		int rnd_flr = fastlog2(val);
		return fastPow2(rnd_flr) == val ? rnd_flr : rnd_flr + 1;
	}
public:
	char* getDataRegion() { return m_dataRegion; }
	DynamicRegion(int _size = DEFAULT_HEAP_SIZE) {
		m_region_size = _size;
		m_dataRegion = new char[m_region_size];
		m_buddy_list_size = fastlog2(DEFAULT_HEAP_SIZE) + 1;
		m_buddy_list = new mem_block*[m_buddy_list_size];
		/*
		Fun fact (that i didn't know): new[] for pointers will initalize data to address 0xCDCDCDCD, not nullptr.
		I wrote the code below to check for nullptr and not 0xCDCDCDCD, so as a consequence i will initialize all the pointers
		to null:
		*/
		for (int i = 0; i < m_buddy_list_size; i++)
			m_buddy_list[i] = nullptr;

		m_buddy_list[m_buddy_list_size - 1] = new mem_block(0, DEFAULT_HEAP_SIZE);//create the first entry, which will be the full size of the region
	}
	mem_block* getBlock(int index, int pos)
	{
		int r = 0;
		mem_block* target = m_buddy_list[index];
		while (target != nullptr && r<pos)
		{
			target = target->next;
			r++;
		}
		return target;
	}
	mem_block* getEnd(int index)
	{
		mem_block* target = m_buddy_list[index];
		while (target != nullptr && target->next != nullptr)
			target = target->next;
		return target;
	}
	mem_block* getByAddress(int address)
	{
		for (int i = 0; i < m_buddy_list_size; i++)
		{
			mem_block * r = m_buddy_list[i];
			while (r != nullptr)
			{
				if (r->index == address)
					return r;
				r = r->next;
			}
		}
		return nullptr;
	}
	mem_block* splitBlock(int index, int pos) {
		if (index >= m_buddy_list_size || index < 0)//first safety check
		{
			std::cerr << "ERROR: INVALID BLOCK SPLIT";
			return 0;
		}
		mem_block * target = getBlock(index, pos);
		if (!target->free)//second safety check
		{
			std::cerr << "ERROR: INVALID BLOCK SPLIT, CANNOT SPLIT AN OCCUPIED BLOCK";
			return 0;
		}
		int newSize = target->size >> 1;// = size/2

		mem_block* nodeL = new mem_block(target->index, newSize);
		mem_block* nodeR = new mem_block(target->index + newSize, newSize);
		target->lChild = nodeL;
		nodeL->parent = target;
		target->rChild = nodeR;
		nodeR->parent = target;

		nodeL->next = nodeR;
		nodeR->prev = nodeL;

		if (m_buddy_list[index - 1] == nullptr)//if no nodes have been allocated here
		{
			m_buddy_list[index - 1] = nodeL;//simply tell that index to point at the left node, which is already doubly linked to the right
		}
		else
		{
			mem_block*  oldHead = m_buddy_list[index - 1];
			m_buddy_list[index - 1] = nodeL;
			nodeR->next = oldHead;
			oldHead->prev = nodeR;
		}
		target->free = false;//since we've split this entry, we will mark it as filled
		return nodeL;//we return the first node split to assign a value to it
	}
	bool isFree(mem_block* target)
	{
		return target->lChild == nullptr && target->rChild == nullptr && target->free == true;//a node is only free if it has no children and is marked as free
	}
	int findAvailableLoc(int index)
	{
		mem_block* search_head = m_buddy_list[index];
		for (int i = 0; search_head != nullptr; i++)
		{
			if (isFree(search_head))
				return i;
			search_head = search_head->next;
		}
		return -1;
	}

	void print_nodes()
	{
		for (int i = 0; i < m_buddy_list_size; i++)
		{
			std::cout << "[" << i << "] - ";
			if (m_buddy_list[i] != nullptr)
			{
				mem_block * t = m_buddy_list[i];
				while (t != nullptr)
				{
					std::cout << "[" << t->index << ", Size: " << t->size << (t->free ? ", FREE" : ", TAKEN") << "] ";
					t = t->next;
				}
			}
			else {
				std::cout << "None";
			}
			std::cout << std::endl;
		}
	}
	int allocate(int amnt) {
		if (amnt <= 0)
		{
			std::cerr << "ERROR: SIZE MUST BE GREATER THAN 0\n";
		}
		int trg_size = getP2(amnt);//first search for first available region in the correct index
								   //we will assume that necessary merging is done at de-allocation
		bool search = true;
		int location;
		if ((location = findAvailableLoc(trg_size)) != -1)//if there is an entry available at this level
		{
			//allocate at the location
			mem_block * target_destination = getBlock(trg_size, location);
			target_destination->free = false;//mark it as full
			int targInd = target_destination->index;
			//m_allocated.push_back(targInd);//add the address we just allocated to our list, for printing purposes
			*(int*)(m_dataRegion + targInd) = amnt;
			return targInd;
		}
		
		//Next, we search for the closest memory large enough to accomodate this request
		int y, x;
		for (y = trg_size + 1; y < m_buddy_list_size && ((x = findAvailableLoc(y)) == -1); y++);//if the current level doesn't have anything, we search up
		if (x != -1)//assuming we've found an open entry
		{
			//due to the way we split blocks, the left most node will always be at the front of our list of available spaces
			splitBlock(y, x);
			for (int i = y - 1; i > trg_size; i--)
			{
				splitBlock(i, 0);
			}

			if (m_buddy_list[trg_size] != nullptr && m_buddy_list[trg_size]->free)
			{
				m_buddy_list[trg_size]->free = false;
				int targInd = m_buddy_list[trg_size]->index;
				//m_allocated.push_back(targInd);
				*(int*)(m_dataRegion + targInd) = amnt;//do not know if this will work, but it should set the bytes to be an integer
				return targInd;
			}
		}
		else
		{
			std::cerr << "ERROR: NOT ENOUGH MEMORY\n";
		}
	}
	void* accessData(int index)
	{
		return m_dataRegion + index;
	}
	~DynamicRegion() {
		delete[] m_dataRegion;
		delete m_buddy_list[m_buddy_list_size - 1];//this should delete all nodes in this tree, as this would be the root node
		delete[] m_buddy_list;
	}
};
int addressID = 1;
class AddressSpace;
struct sharedData
{
	std::vector<AddressSpace*> sharedAmongst;
	data_entry * bss_r;
	data_entry * data_r;
	unsigned char * text_r;
	int text_s;
	int num_using = 0;
	void addProg(AddressSpace* c) {
		sharedAmongst.push_back(c);
		num_using++;
	}
	void notifyLeave()
	{
		num_using--;
	}
};
class AddressSpace {
private:
	/*
	The vectors below are simply here for the print info function: it is not critical to the functioning of the address space
	*/
	int text_addresses_end;
	std::vector<int> bss_addresses;
	std::vector<int> data_addresses;
	std::vector<int> stack_addresses;
	std::vector<int> dynamic_addresses;
	std::string m_processName;
	std::string m_shared1;
	std::string m_shared2;
	MemStack m_stack;
	DynamicRegion m_dynamic;//i tried to implement this as a buddy system
	/*
	Since data and bss do not have entries added/removed from them during runtime, there will only be a fixed number of addresses in this region.
	Entires in bss may also be initalized, and as such must be moved into the data region.
	To avoid complexity, we will store all of these address in one singular array, and move entries as needed to mark where the BSS ends and data begins.
	*/
	int m_bss_end;//indexes used for tracking what the layout of the data region looks like
	int m_data_end;
	memsafe_data_entry* m_dataRegion;//area for BSS and Data, fixed size throughout the execution of the application

	int m_text_end;
	unsigned char* m_text;//this will be an array of bytes, we will be using the char primative as it only consumes one byte of memory in most systems
	sharedData * m_shareStruct;
public:
	/*
	Since i'm using C++, it is expected that the user will pass type information along with the data (or at the very least, the size of these entries).
	In this case, we will use the data_entry struct defined above to do this, which contains an  enum for the type and a void pointer to the data but does not
	de-allocate the memory it contains on destruction. We will also be assumed that the last entry in these arrays will be to a tail sentinal with type T_VOID,
	as this will denote the end of the array for us.

	This peice of code uses several structs to hold data types, and instances of these structs are allocated in arrays to store the values. However, i have taken many
	precations to make it so the code could be easily modified to eliminate this data_entry and memsafe_data_entry struct.
	*/


	std::string getProcessName()
	{
		return m_processName;
	}
	static inline int getSize(data_entry* _array)
	{
		int i;
		for (i = 0; _array[i].dataType != T_VOID; i++);
		return i;
	}
	void setSharingData(sharedData* sharestruct) {
		//first load everything in to the stack
		// 0000 0000
		// <data shared proc1><data shared proc2><text shared proc1><text shared proc2>  <blank><blank><data shared><text shared>
		m_shareStruct = sharestruct;
	}
	AddressSpace(data_entry* stack, int* dynamic, int dynamic_size, data_entry* bss, data_entry* data, unsigned char* text, int text_size) {

		m_processName = "PROCESS"+std::to_string(addressID++);

		int i = 0;
		if (stack != nullptr) {
			while (stack[i].dataType != T_VOID)
			{
				stack_addresses.push_back(STACK_START + i);
				m_stack.push(stack[i]);
				i++;
			}
		}
		
		//next, populate the BSS and data region
		m_bss_end = getSize(bss);
		m_data_end = m_bss_end + getSize(data);
		m_dataRegion = new memsafe_data_entry[m_data_end];//fixed size
		
		for (int i = 0; i < m_bss_end; i++) {
			bss_addresses.push_back(i + BSS_START);
			m_dataRegion[i] = bss[i];
		}
		for (int i = m_bss_end; i < m_data_end; i++) {
			data_addresses.push_back((i-m_bss_end) + DATA_START);
			m_dataRegion[i] = data[i - m_bss_end];
		}
		
		//next, populate dynamic region
		for (int i = 0; i < dynamic_size; i++)
			dynamic_addresses.push_back(m_dynamic.allocate(dynamic[i])+DYNAMIC_START);//allocate necessary memory

		//next, populate text
		//m_text = new unsigned char[text_size];
		m_text = text;//we will NOT be dynamically allocating a seperate copy for this assignment, it will be assumed that our allocator is responsible for 
		m_text_end = text_size;
		text_addresses_end = m_text_end + TEXT_START;
		//memcpy(m_text, text, text_size);
	}
	std::string getSharedDataString()
	{
		std::stringstream c;
		c << "";
		if (m_shareStruct && m_shareStruct->sharedAmongst.size() > 1)
		{
			std::vector<std::string> toBePut;
			c << "(SHARED WITH ";
			for (int i = 0; i < m_shareStruct->sharedAmongst.size(); i++)
			{
				if (m_shareStruct->sharedAmongst[i] != this)
				{
					toBePut.push_back(m_shareStruct->sharedAmongst[i]->getProcessName());
				}
			}
			for (int i = 0; i < toBePut.size(); i++)
			{
				c << toBePut[i] << (i != ((toBePut.size()) - 1) ? ", " : "");
			}
			c << ")";
		}
		
		return c.str();
	}
	void printAddressSpaceInfo() {
		std::cout << "------------------------------"<< m_processName<< " ADDRESS SPACE------------------------------\n";
		std::cout << "TEXT REGION INFO "<<getSharedDataString() <<":\n\n[...]\n";
		for (int i = TEXT_START; i < text_addresses_end; i++)
			std::cout << std::hex << "[0x" << i << "] - " << "["  << "0x" <<(int)*(unsigned char*)accessAddress(i) << std::dec <<"]\n";
		std::cout << "[...]\n";
		
		std::cout << "\nDATA REGION INFO "<<getSharedDataString()<<":\n\n";
		std::cout << "--------------BSS--------------\n[...]\n";
		for (int c : bss_addresses)
			std::cout << std::hex << "[0x" << c << "] - " << "[" << ((memsafe_data_entry*)accessAddress(c))->toString() << std::dec << "]\n";
		std::cout << "[...]\n-------------DATA--------------\n[...]\n";
		for (int c : data_addresses)
			std::cout << std::hex << "[0x" << c << "] - " << "[" << ((memsafe_data_entry*)accessAddress(c))->toString() << std::dec << "]\n";
		std::cout << "[...]\n";
		
		std::cout << "\nDYNAMIC REGION INFO:\n\n";
		std::cout << "BUDDY LAYOUT (NUMBERS ARE OFFSETS):\n";
		m_dynamic.print_nodes();
		std::cout << "[...]\n";
		for (int c : dynamic_addresses)
			std::cout << std::hex << "[0x" << c << "] - " << "[ALLOCATED TO ALLOW " << std::dec << *(int*)accessAddress(c) << " BYTES AT THIS ADDRESS]\n";
		std::cout << "[...]\n";

		std::cout << "\nSTACK REGION INFO:\n\n[...]\n";
		for (int c : stack_addresses)
			std::cout << std::hex << "[0x" << c << "] - " << "[" << ((memsafe_data_entry*)accessAddress(c))->toString() << std::dec << "]\n";
		std::cout << "[...]\n";
		
	}
	/*
	the objective here is to create an address space s.t. there is a seperate set of addresses (indexes) which refer to global addresses (pointers in our case)
	this index will serve as a key (local address) to the some peice of data in memory, we can keep track of a list of taken indexes, then assign the these taken addresses
	to peices in memory
	*/
	void* accessAddress(unsigned int index) {
		//return the real pointer to the relevant address using the local address
		if (TEXT_START <= index && index < BSS_START) {
			//access text
			short local_index = (short)(index - TEXT_START);
			if (local_index < m_text_end)
				return m_text + local_index;
			else
				return nullptr;
		}
		else if (BSS_START <= index && index < DATA_START) {
			//access bss
			unsigned int local_index = index - BSS_START;
			if (local_index < m_bss_end)
				return m_dataRegion + local_index;
			else
				return nullptr;
		}
		else if (DATA_START <= index && index < DYNAMIC_START) {
			//access data
			unsigned int local_index = (index - DATA_START)+m_bss_end;
			if (local_index < m_data_end)
				return m_dataRegion + local_index;
			else
				return nullptr;
		}
		else if (DYNAMIC_START <= index && index < STACK_START) {
			//access dynamic
			unsigned int local_index = index - DYNAMIC_START;
			if (local_index < DEFAULT_HEAP_SIZE)
				return m_dynamic.accessData(local_index);
			else
				return nullptr;
		}
		else if (STACK_START <= index) {
			//access stack
			unsigned int local_index = index - STACK_START;
			if (local_index < m_stack.getMax())
				return &m_stack[local_index];
			else
				return nullptr;
		}
		else {
			//otherwise, the user is attempting to access a null pointer, which is impossible
			std::cerr << "ERROR: CANNOT ACCESS NULL POINTER" << std::endl;
		}
	}
	~AddressSpace()
	{
		//there's a minor issue here: these entries de-allocate each other's data if we're sharing it, which leads to an error upon application close
		if ((m_shareStruct->num_using) <=1)// my solution was to have the last program to use the shared data clean it up
		{
			delete[] m_text;
		}
		else//otherwise
		{
			//we don't delete m_text, since it's the same pointer that's shared
			for (int i = 0; i < m_data_end; i++)//as for the data region, we do still need to de-allocate our memsafe structs
				m_dataRegion[i].data = nullptr;//but we set the data pointers to null before doing so, to ensure that we don't accidentally de-allocate someone elses memory
			//all of this could likely have been solved with smart pointers
		}
		delete[] m_dataRegion;
		m_shareStruct->notifyLeave();
	}
};

std::map<std::string, sharedData> programLinks;
class DataLoader {
public:
	const std::regex int_regex = std::regex("0|(-?[1-9][0-9]*)", std::regex::nosubs);
	const std::regex float_regex = std::regex("-?(([0-9]+)\\\.([0-9]*))(f?)", std::regex::nosubs);
	const std::regex str_regex = std::regex("\"[^\"]*\"", std::regex::nosubs);
	const std::regex char_regex = std::regex("'[^']'", std::regex::nosubs);
	const std::regex byte_regex = std::regex("(\\\\)x[a-fA-F0-9]+", std::regex::nosubs);
	const std::regex anyofabove = std::regex("(-?(([0-9]+)\\\.([0-9]*))(f?))|(0|(-?[1-9][0-9]*))|(\"[^\"]*\")|('[^']')|(\\\\x[a-fA-F0-9]+)", std::regex::nosubs);
	DataType getDType(const std::string & data)
	{
		std::regex testArr[] = { float_regex, int_regex, char_regex, str_regex, byte_regex };
		for (int i = 0; i < 5; i++)
		{
			if (std::regex_match(data, testArr[i]))
				return (DataType)i;//it just so happens that data types are ordered in the same way that our type checks are
		}
		return T_VOID;
	}
	data_entry buildDataEntry(const std::string & data)
	{
		DataType d= getDType(data);
		switch (d)
		{
			case T_FL:
			{
				float f_data = std::stof(data);
				return data_entry(new float(f_data), T_FL);
			}
			case T_INT:
			{
				int i_data = std::stoi(data);
				return data_entry(new int(i_data), T_INT);
			}
			case T_CH:
				return data_entry(new char(data[1]), T_CH);
			case T_STR:
				return data_entry(new std::string(data.substr(1, data.size()-2)), T_STR);
			case T_BYTE:
			{
				unsigned char b_data = std::stoi(data.substr(2), 0, 16);//we skip the \x part 
				return data_entry(new unsigned char(b_data), T_BYTE);
			}
			default:
				return data_entry(nullptr, T_VOID);
			
		}
	}
	void parseDataEntries(const std::string &data, data_entry *& allocated) {
		//let's assume we have a string like 30, -5, 'a', 3.6, "ciao" 

		std::smatch sm;
		std::string iter = std::string(data);
		std::vector<data_entry> found;
		while (std::regex_search(iter, sm, anyofabove))
		{
			if (sm.size() > 0)
			{
				found.push_back(buildDataEntry(sm[0]));
			}
			iter = sm.suffix().str();
		}
		data_entry * c = new data_entry[found.size()+1];
		for (int i = 0; i < found.size(); i++)
			c[i] = found[i];//copy our found entries into the dynamically allocated array
		c[found.size()] = data_entry();//add our tail entry
		allocated = c;
	}
	void paraseBytes(const std::string &data, unsigned char *& allocated, int * size) {
		std::vector<unsigned char> found;
		std::smatch sm;
		std::string iter = std::string(data);
		while (std::regex_search(iter, sm, byte_regex))
		{
			if (sm.size() > 0)
			{
				found.push_back((unsigned char)std::stoi(std::string(sm[0]).substr(2), 0, 16));
			}
			iter = sm.suffix().str();
		}
		*size = found.size();
		unsigned char * byteArray = new unsigned char[found.size()];
		for (int i = 0; i < found.size(); i++)
			byteArray[i] = found[i];
		allocated = byteArray;
	}
	void parseInts(const std::string &data, int *& allocated, int * size) {
		std::vector<int> found;
		std::smatch sm;
		std::string iter = std::string(data);
		while (std::regex_search(iter, sm, int_regex))
		{
			if (sm.size() > 0)
			{
				found.push_back(std::stoi(sm[0]));
			}
			iter = sm.suffix().str();
		}
		*size = found.size();
		int * intArray = new int[found.size()];
		for (int i = 0; i < found.size(); i++)
			intArray[i] = found[i];
		allocated = intArray;
	}

	void initAddressSpace(AddressSpace *& spaceP, const std::string &fpath)
	{
		std::ifstream ifs1(fpath);//load contents from file
		std::string line;
		std::vector<std::string> contents;
		while (std::getline(ifs1, line))
		{
			contents.push_back(line);
		}
		ifs1.close();

		data_entry * stack_r;
		parseDataEntries(contents[0], stack_r);

		int * dynmamic_r;
		int d_size;
		parseInts(contents[1], dynmamic_r, &d_size);
		data_entry *bss_r, *data_r;
		unsigned char * text_r;
		int t_size;
		std::map<std::string, sharedData>::iterator s = programLinks.find(fpath);//build an iterator
		sharedData* sharedDataStruct;
		if (s==programLinks.end())//if there is already an element for this filepath
		{
			//we allocate the data and text region
			parseDataEntries(contents[2], bss_r);
			parseDataEntries(contents[3], data_r);
			paraseBytes(contents[4], text_r, &t_size);

			sharedData newSharedDataStruct;
			newSharedDataStruct.bss_r = bss_r;
			newSharedDataStruct.data_r = data_r;
			newSharedDataStruct.text_r = text_r;
			newSharedDataStruct.text_s = t_size;
			programLinks[fpath] = newSharedDataStruct;//copy our new data struct into the map
			sharedDataStruct = &(programLinks[fpath]);//retrieve the address of our copied entry in the map
		}
		else
		{
			sharedDataStruct = &(programLinks[fpath]);//retrieve the entry we want
			bss_r = sharedDataStruct->bss_r;
			data_r = sharedDataStruct->data_r;
			text_r = sharedDataStruct->text_r;
			t_size = sharedDataStruct->text_s;
		}
		spaceP = new AddressSpace(stack_r, dynmamic_r, d_size, bss_r, data_r, text_r, t_size);
		sharedDataStruct->addProg(spaceP);//add this process to the list of processes using this shared data
		spaceP->setSharingData(sharedDataStruct);//add a reference to this shared data struct to the address space
		std::cout << "LOADED " << spaceP->getProcessName() << " FROM " << fpath << "\n";
	}
};

void address_space_allocation(const std::string &path1, const std::string &path2, const std::string &path3,
	AddressSpace*& p1, AddressSpace*& p2, AddressSpace*& p3) {
	//we've assumed private mapping, so we need to be careful about how we allocate the data and text regions
	//for the sake of this exercise, 
	//*we will assume that the text and dynamic regions are the same amongst all programs
	/*
	We will also assume that the declerations come in the following order:
	Stack: 30, -5, 'a', 3.6, "ciao"
	Dynamic: 900, 50, 10
	BSS: 30, -5, 'a', 3.6, "ciao"
	Data: 30, -5, 'a', 3.6, "ciao"
	Text: \x10, \xa4, \x30

	for simplicity
	*/

	DataLoader loader;
	loader.initAddressSpace(p1, path1);
	//we've initialized the first address space, now we simply load the contents of the other 2, and use our shared data struct to pass the shared regions
	loader.initAddressSpace(p2, path2);
	loader.initAddressSpace(p3, path3);
}
int main(int argc, const char* argv[]) {
	/*
	In this example, we assume that program 1 contains all the shared data (text, bss, data), so we do not copy the data and text regions from programs 2 and 3
	
	To show that the data is shared, programB.txt and programC.txt do not contain any data in their text, data, and BSS regions.

	For the sake of simplicity, I did not write any logic in this code to simulate paging.
	*/
	
	//test.printAddressSpaceInfo();
	if (argc != 1)
	{
		if (argc != 4)
		{
			std::cout << "Error: please specify 3 .txt files to be loaded\n";
			return 0;
		}
		AddressSpace *prog1, *prog2, *prog3;
		address_space_allocation(argv[1], argv[2], argv[3], prog1, prog2, prog3);
		prog1->printAddressSpaceInfo();
		prog2->printAddressSpaceInfo();
		prog3->printAddressSpaceInfo();
		//while (true);
		delete prog1;
		delete prog2;
		delete prog3;
		return 0;
	}
	AddressSpace *prog1, *prog2, *prog3;
	address_space_allocation("programA.txt", "programB.txt", "programB.txt", prog1, prog2, prog3);
	prog1->printAddressSpaceInfo();
	prog2->printAddressSpaceInfo();
	prog3->printAddressSpaceInfo();
	//while (true);
	delete prog1;
	delete prog2;
	delete prog3;
	return 0;

}