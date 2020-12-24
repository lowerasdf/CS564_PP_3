/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University of Wisconsin-Madison.
 */

#include <vector>
#include "btree.h"
#include "page.h"
#include "filescan.h"
#include "page_iterator.h"
#include "file_iterator.h"
#include "exceptions/insufficient_space_exception.h"
#include "exceptions/index_scan_completed_exception.h"
#include "exceptions/file_not_found_exception.h"
#include "exceptions/no_such_key_found_exception.h"
#include "exceptions/bad_scanrange_exception.h"
#include "exceptions/bad_opcodes_exception.h"
#include "exceptions/scan_not_initialized_exception.h"
#include "exceptions/end_of_file_exception.h"

#define checkPassFail(a, b) 																				\
{																																		\
	if(a == b)																												\
		std::cout << "\nTest passed at line no:" << __LINE__ << "\n";		\
	else																															\
	{																																	\
		std::cout << "\nTest FAILS at line no:" << __LINE__;						\
		std::cout << "\nExpected no of records:" << b;									\
		std::cout << "\nActual no of records found:" << a;							\
		std::cout << std::endl;																					\
		exit(1);																												\
	}																																	\
}

using namespace badgerdb;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
const std::string relationName = "relA";
//If the relation size is changed then the second parameter 2 chechPassFail may need to be changed to number of record that are expected to be found during the scan, else tests will erroneously be reported to have failed.
const int	relationSize = 5000;
std::string intIndexName, doubleIndexName, stringIndexName;

// This is the structure for tuples in the base relation

typedef struct tuple {
	int i;
	double d;
	char s[64];
} RECORD;

PageFile* file1;
RecordId rid;
RECORD record1;
std::string dbRecord1;

BufMgr * bufMgr = new BufMgr(100);

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------

void createRelationForward();
void createRelationBackward();
void createRelationRandom();
void intTests();
int intScan(BTreeIndex *index, int lowVal, Operator lowOp, int highVal, Operator highOp);
void indexTests();
void test1();
void test2();
void test3();
void errorTests();
void deleteRelation();


//-----------------------------myTest-----------------------------------------------
void createRelationForward2(int a, int b);
void createRelationForward3(int size);
void createRelationBackward2(int a, int b);
void createRelationBackward3(int size);
void createRelationRandom2(int size);
void EmptyIntTests();
void NegativeIntTests();
void myTest1_LargeRelationForward();
void myTest2_LargeRelationBackward();
void myTest3_LargeRelationRandom();
void myTest4_Empty();
void myTest5_NegativeForward();
void myTest6_NegativeBackward();

int main(int argc, char **argv)
{

  // Clean up from any previous runs that crashed.
  try
	{
    File::remove(relationName);
  }
	catch(const FileNotFoundException &)
	{
  }

	{
		// Create a new database file.
		PageFile new_file = PageFile::create(relationName);

		// Allocate some pages and put data on them.
		for (int i = 0; i < 20; ++i)
		{
			PageId new_page_number;
			Page new_page = new_file.allocatePage(new_page_number);

    	sprintf(record1.s, "%05d string record", i);
    	record1.i = i;
    	record1.d = (double)i;
    	std::string new_data(reinterpret_cast<char*>(&record1), sizeof(record1));

			new_page.insertRecord(new_data);
			new_file.writePage(new_page_number, new_page);
		}

	}
	// new_file goes out of scope here, so file is automatically closed.

	{
		FileScan fscan(relationName, bufMgr);

		try
		{
			RecordId scanRid;
			while(1)
			{
				fscan.scanNext(scanRid);
				//Assuming RECORD.i is our key, lets extract the key, which we know is INTEGER and whose byte offset is also know inside the record. 
				std::string recordStr = fscan.getRecord();
				const char *record = recordStr.c_str();
				int key = *((int *)(record + offsetof (RECORD, i)));
				std::cout << "Extracted : " << key << std::endl;
			}
		}
		catch(const EndOfFileException &e)
		{
			std::cout << "Read all records" << std::endl;
		}
	}
	// filescan goes out of scope here, so relation file gets closed.

	File::remove(relationName);

	test1();
	test2();
	test3();
	errorTests();
	
	//----------------myTests-----------------------
	myTest1_LargeRelationForward();
	myTest2_LargeRelationBackward();
    myTest3_LargeRelationRandom();
	//This test is still problematic
	//myTest4_Empty();
	myTest5_NegativeForward();
	myTest6_NegativeBackward();

	delete bufMgr;

  return 1;
}

void test1()
{
	// Create a relation with tuples valued 0 to relationSize and perform index tests 
	// on attributes of all three types (int, double, string)
	std::cout << "---------------------" << std::endl;
	std::cout << "createRelationForward" << std::endl;
	createRelationForward();
	indexTests();
	deleteRelation();
}

void test2()
{
	// Create a relation with tuples valued 0 to relationSize in reverse order and perform index tests 
	// on attributes of all three types (int, double, string)
	std::cout << "----------------------" << std::endl;
	std::cout << "createRelationBackward" << std::endl;
	createRelationBackward();
	indexTests();
	deleteRelation();
}

void test3()
{
	// Create a relation with tuples valued 0 to relationSize in random order and perform index tests 
	// on attributes of all three types (int, double, string)
	std::cout << "--------------------" << std::endl;
	std::cout << "createRelationRandom" << std::endl;
	createRelationRandom();
	indexTests();
	deleteRelation();
}

// -----------------------------------------------------------------------------
// createRelationForward
// -----------------------------------------------------------------------------

void createRelationForward()
{
	std::vector<RecordId> ridVec;
  // destroy any old copies of relation file
	try
	{
		File::remove(relationName);
	}
	catch(const FileNotFoundException &e)
	{
	}

  file1 = new PageFile(relationName, true);

  // initialize all of record1.s to keep purify happy
  memset(record1.s, ' ', sizeof(record1.s));
	PageId new_page_number;
  Page new_page = file1->allocatePage(new_page_number);

  // Insert a bunch of tuples into the relation.
  for(int i = 0; i < relationSize; i++ )
	{
    sprintf(record1.s, "%05d string record", i);
    record1.i = i;
    record1.d = (double)i;
    std::string new_data(reinterpret_cast<char*>(&record1), sizeof(record1));

		while(1)
		{
			try
			{
    		new_page.insertRecord(new_data);
				break;
			}
			catch(const InsufficientSpaceException &e)
			{
				file1->writePage(new_page_number, new_page);
  			new_page = file1->allocatePage(new_page_number);
			}
		}
  }

	file1->writePage(new_page_number, new_page);
}

// -----------------------------------------------------------------------------
// createRelationBackward
// -----------------------------------------------------------------------------

void createRelationBackward()
{
  // destroy any old copies of relation file
	try
	{
		File::remove(relationName);
	}
	catch(const FileNotFoundException &e)
	{
	}
  file1 = new PageFile(relationName, true);

  // initialize all of record1.s to keep purify happy
  memset(record1.s, ' ', sizeof(record1.s));
	PageId new_page_number;
  Page new_page = file1->allocatePage(new_page_number);

  // Insert a bunch of tuples into the relation.
  for(int i = relationSize - 1; i >= 0; i-- )
	{
    sprintf(record1.s, "%05d string record", i);
    record1.i = i;
    record1.d = i;

    std::string new_data(reinterpret_cast<char*>(&record1), sizeof(RECORD));

		while(1)
		{
			try
			{
    		new_page.insertRecord(new_data);
				break;
			}
			catch(const InsufficientSpaceException &e)
			{
				file1->writePage(new_page_number, new_page);
  			new_page = file1->allocatePage(new_page_number);
			}
		}
  }

	file1->writePage(new_page_number, new_page);
}

// -----------------------------------------------------------------------------
// createRelationRandom
// -----------------------------------------------------------------------------

void createRelationRandom()
{
  // destroy any old copies of relation file
	try
	{
		File::remove(relationName);
	}
	catch(const FileNotFoundException &e)
	{
	}
  file1 = new PageFile(relationName, true);

  // initialize all of record1.s to keep purify happy
  memset(record1.s, ' ', sizeof(record1.s));
	PageId new_page_number;
  Page new_page = file1->allocatePage(new_page_number);

  // insert records in random order

  std::vector<int> intvec(relationSize);
  for( int i = 0; i < relationSize; i++ )
  {
    intvec[i] = i;
  }

  long pos;
  int val;
	int i = 0;
  while( i < relationSize )
  {
    pos = random() % (relationSize-i);
    val = intvec[pos];
    sprintf(record1.s, "%05d string record", val);
    record1.i = val;
    record1.d = val;

    std::string new_data(reinterpret_cast<char*>(&record1), sizeof(RECORD));

		while(1)
		{
			try
			{
    		new_page.insertRecord(new_data);
				break;
			}
			catch(const InsufficientSpaceException &e)
			{
      	file1->writePage(new_page_number, new_page);
  			new_page = file1->allocatePage(new_page_number);
			}
		}

		int temp = intvec[relationSize-1-i];
		intvec[relationSize-1-i] = intvec[pos];
		intvec[pos] = temp;
		i++;
  }
  
	file1->writePage(new_page_number, new_page);
}

// -----------------------------------------------------------------------------
// indexTests
// -----------------------------------------------------------------------------

void indexTests()
{
  intTests();
	try
	{
		File::remove(intIndexName);
	}
  catch(const FileNotFoundException &e)
  {
  }
}

// -----------------------------------------------------------------------------
// intTests
// -----------------------------------------------------------------------------

void intTests()
{
  std::cout << "Create a B+ Tree index on the integer field" << std::endl;
  BTreeIndex index(relationName, intIndexName, bufMgr, offsetof(tuple,i), INTEGER);

	// run some tests
	checkPassFail(intScan(&index,25,GT,40,LT), 14)
	checkPassFail(intScan(&index,20,GTE,35,LTE), 16)
	checkPassFail(intScan(&index,-3,GT,3,LT), 3)
	checkPassFail(intScan(&index,996,GT,1001,LT), 4)
	checkPassFail(intScan(&index,0,GT,1,LT), 0)
	checkPassFail(intScan(&index,300,GT,400,LT), 99)
	checkPassFail(intScan(&index,3000,GTE,4000,LT), 1000)
}

int intScan(BTreeIndex * index, int lowVal, Operator lowOp, int highVal, Operator highOp)
{
  RecordId scanRid;
	Page *curPage;

  std::cout << "Scan for ";
  if( lowOp == GT ) { std::cout << "("; } else { std::cout << "["; }
  std::cout << lowVal << "," << highVal;
  if( highOp == LT ) { std::cout << ")"; } else { std::cout << "]"; }
  std::cout << std::endl;

  int numResults = 0;
	
	try
	{
  	index->startScan(&lowVal, lowOp, &highVal, highOp);
	}
	catch(const NoSuchKeyFoundException &e)
	{
    std::cout << "No Key Found satisfying the scan criteria." << std::endl;
		return 0;
	}

	while(1)
	{
		try
		{
			index->scanNext(scanRid);
			bufMgr->readPage(file1, scanRid.page_number, curPage);
			RECORD myRec = *(reinterpret_cast<const RECORD*>(curPage->getRecord(scanRid).data()));
			bufMgr->unPinPage(file1, scanRid.page_number, false);

			if( numResults < 5 )
			{
				std::cout << "at:" << scanRid.page_number << "," << scanRid.slot_number;
				std::cout << " -->:" << myRec.i << ":" << myRec.d << ":" << myRec.s << ":" <<std::endl;
			}
			else if( numResults == 5 )
			{
				std::cout << "..." << std::endl;
			}
		}
		catch(const IndexScanCompletedException &e)
		{
			break;
		}

		numResults++;
	}

  if( numResults >= 5 )
  {
    std::cout << "Number of results: " << numResults << std::endl;
  }
  index->endScan();
  std::cout << std::endl;

	return numResults;
}

// -----------------------------------------------------------------------------
// errorTests
// -----------------------------------------------------------------------------

void errorTests()
{
	{
		std::cout << "Error handling tests" << std::endl;
		std::cout << "--------------------" << std::endl;
		// Given error test

		try
		{
			File::remove(relationName);
		}
		catch(const FileNotFoundException &e)
		{
		}

		file1 = new PageFile(relationName, true);
		
		// initialize all of record1.s to keep purify happy
		memset(record1.s, ' ', sizeof(record1.s));
		PageId new_page_number;
		Page new_page = file1->allocatePage(new_page_number);

		// Insert a bunch of tuples into the relation.
		for(int i = 0; i <10; i++ ) 
		{
		  sprintf(record1.s, "%05d string record", i);
		  record1.i = i;
		  record1.d = (double)i;
		  std::string new_data(reinterpret_cast<char*>(&record1), sizeof(record1));

			while(1)
			{
				try
				{
		  		new_page.insertRecord(new_data);
					break;
				}
				catch(const InsufficientSpaceException &e)
				{
					file1->writePage(new_page_number, new_page);
					new_page = file1->allocatePage(new_page_number);
				}
			}
		}

		file1->writePage(new_page_number, new_page);

		BTreeIndex index(relationName, intIndexName, bufMgr, offsetof(tuple,i), INTEGER);
		
		int int2 = 2;
		int int5 = 5;

		// Scan Tests
		std::cout << "Call endScan before startScan" << std::endl;
		try
		{
			index.endScan();
			std::cout << "ScanNotInitialized Test 1 Failed." << std::endl;
		}
		catch(const ScanNotInitializedException &e)
		{
			std::cout << "ScanNotInitialized Test 1 Passed." << std::endl;
		}
		
		std::cout << "Call scanNext before startScan" << std::endl;
		try
		{
			RecordId foo;
			index.scanNext(foo);
			std::cout << "ScanNotInitialized Test 2 Failed." << std::endl;
		}
		catch(const ScanNotInitializedException &e)
		{
			std::cout << "ScanNotInitialized Test 2 Passed." << std::endl;
		}
		
		std::cout << "Scan with bad lowOp" << std::endl;
		try
		{
			index.startScan(&int2, LTE, &int5, LTE);
			std::cout << "BadOpcodesException Test 1 Failed." << std::endl;
		}
		catch(const BadOpcodesException &e)
		{
			std::cout << "BadOpcodesException Test 1 Passed." << std::endl;
		}
		
		std::cout << "Scan with bad highOp" << std::endl;
		try
		{
			index.startScan(&int2, GTE, &int5, GTE);
			std::cout << "BadOpcodesException Test 2 Failed." << std::endl;
		}
		catch(const BadOpcodesException &e)
		{
			std::cout << "BadOpcodesException Test 2 Passed." << std::endl;
		}


		std::cout << "Scan with bad range" << std::endl;
		try
		{
			index.startScan(&int5, GTE, &int2, LTE);
			std::cout << "BadScanrangeException Test 1 Failed." << std::endl;
		}
		catch(const BadScanrangeException &e)
		{
			std::cout << "BadScanrangeException Test 1 Passed." << std::endl;
		}

		deleteRelation();
	}

	try
	{
		File::remove(intIndexName);
	}
  catch(const FileNotFoundException &e)
  {
  }
}

void deleteRelation()
{
	if(file1)
	{
		bufMgr->flushFile(file1);
		delete file1;
		file1 = NULL;
	}
	try
	{
		File::remove(relationName);
	}
	catch(const FileNotFoundException &e)
	{
	}
}










//---------------------------------myTest--------------------------------------





// -----------------------------------------------------------------------------
// createRelationForward2
// -----------------------------------------------------------------------------
void createRelationForward2(int a, int b)
{
	std::vector<RecordId> ridVec;
	// destroy any old copies of relation file
	try
	{
		File::remove(relationName);
	}
	catch (const FileNotFoundException& e)
	{
	}

	file1 = new PageFile(relationName, true);

	// initialize all of record1.s to keep purify happy
	memset(record1.s, ' ', sizeof(record1.s));
	PageId new_page_number;
	Page new_page = file1->allocatePage(new_page_number);

	// Insert a bunch of tuples into the relation.
	for (int i = a; i < b; i++)
	{
		sprintf(record1.s, "%05d string record", i);
		record1.i = i;
		record1.d = (double)i;
		std::string new_data(reinterpret_cast<char*>(&record1), sizeof(record1));

		while (1)
		{
			try
			{
				new_page.insertRecord(new_data);
				break;
			}
			catch (const InsufficientSpaceException& e)
			{
				file1->writePage(new_page_number, new_page);
				new_page = file1->allocatePage(new_page_number);
			}
		}
	}

	file1->writePage(new_page_number, new_page);
}


// -----------------------------------------------------------------------------
// createRelationForward3
// -----------------------------------------------------------------------------

void createRelationForward3(int size)
{
	std::vector<RecordId> ridVec;
	// destroy any old copies of relation file
	try
	{
		File::remove(relationName);
	}
	catch (const FileNotFoundException& e)
	{
	}

	file1 = new PageFile(relationName, true);

	// initialize all of record1.s to keep purify happy
	memset(record1.s, ' ', sizeof(record1.s));
	PageId new_page_number;
	Page new_page = file1->allocatePage(new_page_number);

	// Insert a bunch of tuples into the relation.
	for (int i = 0; i < size; i++)
	{
		sprintf(record1.s, "%05d string record", i);
		record1.i = i;
		record1.d = (double)i;
		std::string new_data(reinterpret_cast<char*>(&record1), sizeof(record1));

		while (1)
		{
			try
			{
				new_page.insertRecord(new_data);
				break;
			}
			catch (const InsufficientSpaceException& e)
			{
				file1->writePage(new_page_number, new_page);
				new_page = file1->allocatePage(new_page_number);
			}
		}
	}

	file1->writePage(new_page_number, new_page);
}

// -----------------------------------------------------------------------------
// createRelationBackward2
// -----------------------------------------------------------------------------

void createRelationBackward2(int a, int b)
{
	// destroy any old copies of relation file
	try
	{
		File::remove(relationName);
	}
	catch (const FileNotFoundException& e)
	{
	}
	file1 = new PageFile(relationName, true);

	// initialize all of record1.s to keep purify happy
	memset(record1.s, ' ', sizeof(record1.s));
	PageId new_page_number;
	Page new_page = file1->allocatePage(new_page_number);

	// Insert a bunch of tuples into the relation.
	for (int i = b - 1; i >= a; i--)
	{
		sprintf(record1.s, "%05d string record", i);
		record1.i = i;
		record1.d = i;

		std::string new_data(reinterpret_cast<char*>(&record1), sizeof(RECORD));

		while (1)
		{
			try
			{
				new_page.insertRecord(new_data);
				break;
			}
			catch (const InsufficientSpaceException& e)
			{
				file1->writePage(new_page_number, new_page);
				new_page = file1->allocatePage(new_page_number);
			}
		}
	}

	file1->writePage(new_page_number, new_page);
}

// -----------------------------------------------------------------------------
// createRelationBackward3
// -----------------------------------------------------------------------------

void createRelationBackward3(int size)
{
	// destroy any old copies of relation file
	try
	{
		File::remove(relationName);
	}
	catch (const FileNotFoundException& e)
	{
	}
	file1 = new PageFile(relationName, true);

	// initialize all of record1.s to keep purify happy
	memset(record1.s, ' ', sizeof(record1.s));
	PageId new_page_number;
	Page new_page = file1->allocatePage(new_page_number);

	// Insert a bunch of tuples into the relation.
	for (int i = size - 1; i >= 0; i--)
	{
		sprintf(record1.s, "%05d string record", i);
		record1.i = i;
		record1.d = i;

		std::string new_data(reinterpret_cast<char*>(&record1), sizeof(RECORD));

		while (1)
		{
			try
			{
				new_page.insertRecord(new_data);
				break;
			}
			catch (const InsufficientSpaceException& e)
			{
				file1->writePage(new_page_number, new_page);
				new_page = file1->allocatePage(new_page_number);
			}
		}
	}

	file1->writePage(new_page_number, new_page);
}



// -----------------------------------------------------------------------------
// createRelationRandom2
// -----------------------------------------------------------------------------
void createRelationRandom2(int size)
{
	// destroy any old copies of relation file
	try
	{
		File::remove(relationName);
	}
	catch (const FileNotFoundException& e)
	{
	}
	file1 = new PageFile(relationName, true);

	// initialize all of record1.s to keep purify happy
	memset(record1.s, ' ', sizeof(record1.s));
	PageId new_page_number;
	Page new_page = file1->allocatePage(new_page_number);

	// insert records in random order

	std::vector<int> intvec(size);
	for (int i = 0; i < size; i++)
	{
		intvec[i] = i;
	}

	long pos;
	int val;
	int i = 0;
	while (i < size)
	{
		pos = random() % (size - i);
		val = intvec[pos];
		sprintf(record1.s, "%05d string record", val);
		record1.i = val;
		record1.d = val;

		std::string new_data(reinterpret_cast<char*>(&record1), sizeof(RECORD));

		while (1)
		{
			try
			{
				new_page.insertRecord(new_data);
				break;
			}
			catch (const InsufficientSpaceException& e)
			{
				file1->writePage(new_page_number, new_page);
				new_page = file1->allocatePage(new_page_number);
			}
		}

		int temp = intvec[size - 1 - i];
		intvec[size - 1 - i] = intvec[pos];
		intvec[pos] = temp;
		i++;
	}

	file1->writePage(new_page_number, new_page);
}


// -----------------------------------------------------------------------------
// EmptyIntTests()
// -----------------------------------------------------------------------------

void EmptyIntTests()
{
	std::cout << "Create a B+ Tree index on the integer field" << std::endl;
	BTreeIndex index(relationName, intIndexName, bufMgr, offsetof(tuple, i), INTEGER);

	// run some tests
	checkPassFail(intScan(&index, 25, GT, 40, LT), 0)
		checkPassFail(intScan(&index, 20, GTE, 35, LTE), 0)
		checkPassFail(intScan(&index, -3, GT, 3, LT), 0)
		checkPassFail(intScan(&index, 996, GT, 1001, LT), 0)
		checkPassFail(intScan(&index, 0, GT, 1, LT), 0)
		checkPassFail(intScan(&index, 300, GT, 400, LT), 0)
		checkPassFail(intScan(&index, 3000, GTE, 4000, LT), 0)
}

// -----------------------------------------------------------------------------
// NegativeIntTests()
// -----------------------------------------------------------------------------

void NegativeIntTests()
{
	std::cout << "Create a B+ Tree index on the integer field" << std::endl;
	BTreeIndex index(relationName, intIndexName, bufMgr, offsetof(tuple, i), INTEGER);

	// run some tests
	    checkPassFail(intScan(&index, -10, GT, 10, LT), 19)
		checkPassFail(intScan(&index, -1, GTE, 0, LTE), 2)
		checkPassFail(intScan(&index, -3, GT, 3, LT), 5)
		checkPassFail(intScan(&index, -1, GT, 0, LT), 0)
		checkPassFail(intScan(&index, -3000, GTE, 1000, LT), 4000)
}







void myTest1_LargeRelationForward()
{
	// Create a relation with no tuples
	std::cout << "---------------------" << std::endl;
	std::cout << "create a relation forward with larger size" << std::endl;
	createRelationForward3(20000);
	intTests();
	deleteRelation();
}

void myTest2_LargeRelationBackward()
{
	// Create a relation with no tuples
	std::cout << "---------------------" << std::endl;
	std::cout << "create a relation backward with larger size" << std::endl;
	createRelationBackward3(20000);
	intTests();
	deleteRelation();
}

void myTest3_LargeRelationRandom()
{
	// Create a relation with no tuples
	std::cout << "---------------------" << std::endl;
	std::cout << "create a relation random with larger size" << std::endl;
	createRelationRandom2(20000);
	intTests();
	deleteRelation();
}

void myTest4_Empty()
{
	// Create a relation with no tuples
	std::cout << "---------------------" << std::endl;
	std::cout << "create an empty relation" << std::endl;
	createRelationBackward3(0);
	EmptyIntTests();
	deleteRelation();
}

void myTest5_NegativeForward()
{
	// Create a relation with no tuples
	std::cout << "---------------------" << std::endl;
	std::cout << "create a relation forward with neagtive numbers" << std::endl;
	createRelationForward2(-5000,5000);
	NegativeIntTests();
	deleteRelation();
}

void myTest6_NegativeBackward()
{
	// Create a relation with no tuples
	std::cout << "---------------------" << std::endl;
	std::cout << "create a relation backward with neagtive numbers" << std::endl;
	createRelationBackward2(-5000, 5000);
	NegativeIntTests();
	deleteRelation();
}





