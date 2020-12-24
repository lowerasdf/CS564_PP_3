/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University of Wisconsin-Madison.
 */

#include "btree.h"
#include "filescan.h"
#include "exceptions/bad_index_info_exception.h"
#include "exceptions/bad_opcodes_exception.h"
#include "exceptions/bad_scanrange_exception.h"
#include "exceptions/no_such_key_found_exception.h"
#include "exceptions/scan_not_initialized_exception.h"
#include "exceptions/index_scan_completed_exception.h"
#include "exceptions/file_not_found_exception.h"
#include "exceptions/end_of_file_exception.h"


//#define DEBUG

namespace badgerdb
{

// -----------------------------------------------------------------------------
// BTreeIndex::BTreeIndex -- Constructor
// -----------------------------------------------------------------------------

BTreeIndex::BTreeIndex(const std::string & relationName,
		std::string & outIndexName,
		BufMgr *bufMgrIn,
		const int attrByteOffset,
		const Datatype attrType)
{
	bufMgr = bufMgrIn;
	this->attrByteOffset = attrByteOffset;
	attributeType = attrType;
	leafOccupancy = INTARRAYLEAFSIZE;
	nodeOccupancy = INTARRAYNONLEAFSIZE;
	scanExecuting = false;

	///constructing the index name 
	std::ostringstream idxStr;
	idxStr << relationName << "." << attrByteOffset;
	outIndexName = idxStr.str();

	//pointer pointing to the page containing metadata
	Page* metadataPage;
	Page* rootPage;
	IndexMetaInfo* metadata;

	///if the index file exists
	try {

		file = new BlobFile(outIndexName, false);

		//get the pid of the first page
		headerPageNum = file->getFirstPageNo();
		//read the header page and set metadataPage as a pointer to that page
		bufMgr->readPage(file, headerPageNum, metadataPage);

		metadata = (IndexMetaInfo*)metadataPage;
		rootPageNum = metadata->rootPageNo;

		bufMgr->unPinPage(file, headerPageNum, false);
	}

	//if the index file does not exists, create a new one
	catch (FileNotFoundException& e) {
		file = new BlobFile(outIndexName, true);

		bufMgr->allocPage(file, headerPageNum, metadataPage);
		memset(metadataPage, 0, Page::SIZE);
		metadata = (IndexMetaInfo*)metadataPage;

		bufMgr->allocPage(file, rootPageNum, (Page *&)rootPage);
		memset(rootPage, 0, Page::SIZE);

		//set the meta variables
		strncpy(metadata->relationName, relationName.c_str(), 20);
		metadata->attrByteOffset = attrByteOffset;
		metadata->attrType = attrType;
		metadata->rootPageNo = rootPageNum;

		//initialize the root node
		LeafNodeInt* root = (LeafNodeInt*)rootPage;
		root->size = 0;
		rootIsLeaf = true;

		bufMgr->unPinPage(file, headerPageNum, true);
		bufMgr->unPinPage(file, rootPageNum, true);

		//create a new file scanner
		FileScan fscan(relationName, bufMgr);
		try {
			RecordId scanRid;
			while (1) {
				fscan.scanNext(scanRid);
				std::string record = fscan.getRecord();
				insertEntry(record.c_str() + attrByteOffset, scanRid);
			}
		}
		catch (EndOfFileException& e) {
			//do nothing
		}
	}

}


// -----------------------------------------------------------------------------
// BTreeIndex::~BTreeIndex -- destructor
// -----------------------------------------------------------------------------

BTreeIndex::~BTreeIndex()
{
	/// stop the ongoing scanning
	if (scanExecuting == true) {
		endScan();
	}
	/// flush and delete the object file
	bufMgr->flushFile(file);
	delete file;
}

/**
* Insert a key and a record ID to the appropriate position and array of the given leaf node.
*
* @param key   key to be inserted to keyArray
* @param rid   rid to be inserted to ridArray
* @param node  corresponding leaf node to be modified
* @param index index of a particular key and rid to be inserted
*/
void BTreeIndex::insertLeafNode(int key,
				    RecordId rid,
				    LeafNodeInt* node,
					int index) {
	// Shift to the right key after index and insert key at the appropriate position
	memmove(&node->keyArray[index + 1], &node->keyArray[index], sizeof(int) * (INTARRAYLEAFSIZE - index - 1));
	node->keyArray[index] = key;
	
	// Do the same for rid
	memmove(&node->ridArray[index + 1], &node->ridArray[index], sizeof(RecordId) * (INTARRAYLEAFSIZE - index - 1));
	node->ridArray[index] = rid;

	// Increment size
	node->size += 1;
}

/**
* Insert a key and a page ID to the appropriate position and array of the given non-leaf node.
*
* @param key   key to be inserted to keyArray
* @param childPageId   pageid to be inserted to pageNoArray
* @param node  corresponding non-leaf node to be modified
* @param index index of a particular key and pageid to be inserted
*/
void BTreeIndex::insertNonLeafNode(int key,
				    PageId childPageId,
					NonLeafNodeInt* node,
					int index) {
	// Shift to the right key after index and insert key at the appropriate position
	memmove(&node->keyArray[index + 1], &node->keyArray[index], sizeof(int) * (INTARRAYNONLEAFSIZE - index - 1));
	node->keyArray[index] = key;
	
	// Do the same for pid inside pageNoArray
	memmove(&node->pageNoArray[index + 2], &node->pageNoArray[index + 1], sizeof(PageId) * (INTARRAYNONLEAFSIZE - index - 1));
	node->pageNoArray[index + 1] = childPageId;

	// Increment size
	node->size += 1;
}

/**
* Helper function to do the recurion of inserting a key and a rid to the tree.
*
* @param key   key to be inserted
* @param rid   rid to be inserted
* @param currPageId  the current page which holds the node
* @param middleValueFromChild a callback parameter to pass back the middle value to its parent
* @param newlyCreatedPageId a callback parameter to pass back the splitted node's pageID to its parent
* @param isLeafBool whether this node is a leaf or not
*/
void BTreeIndex::insertEntryHelper(const int key,
					const RecordId rid,
					PageId currPageId,
					int* middleValueFromChild,
					PageId* newlyCreatedPageId,
					bool isLeafBool) 
{
	// Read page, which is a node
	Page *currNode;
  	bufMgr->readPage(file, currPageId, currNode);

  	// Base Case: current node is a leaf node
  	if (isLeafBool) {
  		LeafNodeInt* currLeafNode = (LeafNodeInt *)currNode;

  		// Index of the new key to be inserted
  		int index = currLeafNode->size;
  		for(int i = 0; i < currLeafNode->size; i++) {
  			if (currLeafNode->keyArray[i] > key) {
  				index = i;
  				break;
  			}
  		}

  		// Check if leaf is not full, if true directly insert to the leaf
  		if (currLeafNode->ridArray[INTARRAYLEAFSIZE - 1].page_number == Page::INVALID_NUMBER) {
  		//if (currLeafNode->size < INTARRAYLEAFSIZE) {	
			insertLeafNode(key, rid, currLeafNode, index);
  			bufMgr->unPinPage(file, currPageId, true);
  			*middleValueFromChild = 0;
  			*newlyCreatedPageId = 0;
  		} 
  		// Otherwise, split node into 2 and pass back the middle value
  		else {
  			// Allocate a new page for the right split
  			PageId newPageId;
  			Page* newNode;
  			bufMgr->allocPage(file, newPageId, newNode);
  			memset(newNode, 0, Page::SIZE);
  			LeafNodeInt* newLeafNode = (LeafNodeInt *)newNode;

  			// Split and add new value appropriately depending on the position of the index
  			int mid = INTARRAYLEAFSIZE / 2;
  			if (INTARRAYLEAFSIZE % 2 == 1 && index > mid) {
				mid = mid + 1;
			}

			for(int i = mid; i < INTARRAYLEAFSIZE; i++) {
				newLeafNode->keyArray[i-mid] = currLeafNode->keyArray[i];
				newLeafNode->ridArray[i-mid] = currLeafNode->ridArray[i];
				currLeafNode->keyArray[i] = 0;
				currLeafNode->ridArray[i].page_number = Page::INVALID_NUMBER;
			}

  			// Set size appropriately
  			newLeafNode->size = INTARRAYLEAFSIZE - mid;
	  		currLeafNode->size = mid;

			// Insert to right node
			if(index > INTARRAYLEAFSIZE / 2) {
				insertLeafNode(key, rid, newLeafNode, index - mid);
			}
			// Insert to left node
			else {
				insertLeafNode(key, rid, currLeafNode, index);
			}

			// Set rightSibPageNo appropriately
			newLeafNode->rightSibPageNo = currLeafNode->rightSibPageNo;
  			currLeafNode->rightSibPageNo = newPageId;

  			// Unpin the nodes
  			bufMgr->unPinPage(file, currPageId, true);
  			bufMgr->unPinPage(file, newPageId, true);

  			// Return values using pointers
  			*middleValueFromChild = newLeafNode->keyArray[0];
  			*newlyCreatedPageId = newPageId;
  		}
  	}
  	// Recursive Case: current node is not a leaf node
  	else {
  		NonLeafNodeInt *currNonLeafNode = (NonLeafNodeInt *)currNode;

  		// Find the correct child node
  		int childIndex = currNonLeafNode->size;
  		for(int i = 0; i < currNonLeafNode->size; i++) {
  			if (currNonLeafNode->keyArray[i] > key) {
  				childIndex = i;
  				break;
  			}
  		}
  		PageId currChildId = currNonLeafNode->pageNoArray[childIndex];

  		// Recursive call to the child
		int newChildMiddleKey;
		PageId newChildId;
		insertEntryHelper(key, rid, currChildId, &newChildMiddleKey, &newChildId, currNonLeafNode->level == 1);

		// If there is no split in child node
		if ((int) newChildId == 0) {
		  bufMgr->unPinPage(file, currPageId, false);
		  *middleValueFromChild = 0;
		  *newlyCreatedPageId = 0;
		}
		// If there is a split in child node
		else {
	  		// Index of the new middle key from children to be inserted
	  		int index = currNonLeafNode->size;
	  		for(int i = 0; i < currNonLeafNode->size; i++) {
	  			if (currNonLeafNode->keyArray[i] > newChildMiddleKey) {
	  				index = i;
	  				break;
	  			}
	  		}

	  		// Check if node is not full, if true directly insert middle key to the node
  			if (currNonLeafNode->pageNoArray[INTARRAYNONLEAFSIZE] == Page::INVALID_NUMBER) {
  			//if (currNonLeafNode->size < INTARRAYNONLEAFSIZE) {
  				insertNonLeafNode(newChildMiddleKey, newChildId, currNonLeafNode, index);
  				bufMgr->unPinPage(file, currPageId, true);
	  			*middleValueFromChild = 0;
	  			*newlyCreatedPageId = 0;
  			}
  			// Otherwise, split node into 2 and pass back the middle value
  			else {
	  			// Allocate a new page for the right split
	  			PageId newPageId;
	  			Page* newNode;
	  			bufMgr->allocPage(file, newPageId, newNode);
	  			memset(newNode, 0, Page::SIZE);
	  			NonLeafNodeInt* newNonLeafNode = (NonLeafNodeInt *)newNode;
	  			newNonLeafNode->level = currNonLeafNode->level;

	  			// Split nodes depending on the cases
	  			int mid = INTARRAYNONLEAFSIZE / 2;
	  			// Case 1: the new child will be pushed
	  			if (index == mid) {
	  				for(int i = mid; i < INTARRAYNONLEAFSIZE; i++) {
						newNonLeafNode->keyArray[i-mid] = currNonLeafNode->keyArray[i];
						newNonLeafNode->pageNoArray[i-mid+1] = currNonLeafNode->pageNoArray[i+1];
						currNonLeafNode->keyArray[i] = 0;
						currNonLeafNode->pageNoArray[i+1] = Page::INVALID_NUMBER;
					}
					newNonLeafNode->pageNoArray[0] = newChildId;

					// Set size appropriately
					currNonLeafNode->size = mid;
					newNonLeafNode->size = INTARRAYNONLEAFSIZE - mid;

		  			// Unpin the nodes
		  			bufMgr->unPinPage(file, currPageId, true);
		  			bufMgr->unPinPage(file, newPageId, true);

					// Return values using pointers
		  			*middleValueFromChild = newChildMiddleKey;
		  			*newlyCreatedPageId = newPageId;
	  			}
	  			// Case 2: if new child will not be pushed
	  			else {
	  				if (INTARRAYNONLEAFSIZE % 2 == 0 && index < mid) {
	  					mid -= 1;
	  				}
	  				for(int i = mid + 1; i < INTARRAYNONLEAFSIZE; i++) {
						newNonLeafNode->keyArray[i-mid-1] = currNonLeafNode->keyArray[i];
						newNonLeafNode->pageNoArray[i-mid-1] = currNonLeafNode->pageNoArray[i];
						currNonLeafNode->keyArray[i] = 0;
						currNonLeafNode->pageNoArray[i] = Page::INVALID_NUMBER;
					}

					// Return values using pointers
					*middleValueFromChild = currNonLeafNode->keyArray[mid];
					*newlyCreatedPageId = newPageId;

					// Clear already pushed value
					currNonLeafNode->keyArray[mid] = 0;

					// Set size appropriately
					currNonLeafNode->size = mid;
					newNonLeafNode->size = INTARRAYNONLEAFSIZE - mid - 1;

					// Insert new value to left or right node appropriately
					if (index < INTARRAYNONLEAFSIZE / 2) {
						insertNonLeafNode(newChildMiddleKey, newChildId, currNonLeafNode, index);
					} else {
						insertNonLeafNode(newChildMiddleKey, newChildId, newNonLeafNode, index - mid);
					}

		  			// Unpin the nodes
		  			bufMgr->unPinPage(file, currPageId, true);
		  			bufMgr->unPinPage(file, newPageId, true);
	  			}
  			}
		}
  	}
}

// -----------------------------------------------------------------------------
// BTreeIndex::insertEntry
// -----------------------------------------------------------------------------

void BTreeIndex::insertEntry(const void *key, const RecordId rid) 
{
	// Call the helper function to do the recursion while retrieving back new middle value and pageId if there is a split
	int middleValueFromChild;
	PageId newlyCreatedPageId;
	insertEntryHelper(*(int*)key, rid, rootPageNum, &middleValueFromChild, &newlyCreatedPageId, rootIsLeaf);

	// If there is a split to the root, create a new root
	if ((int) newlyCreatedPageId != 0) {
	  	// Allocate a new page for this new root
	  	PageId newPageId;
		Page* newPage;
		bufMgr->allocPage(file, newPageId, newPage);
		memset(newPage, 0, Page::SIZE);
		NonLeafNodeInt* newRoot = (NonLeafNodeInt *)newPage;
		
		// Update the new page appropriately
		newRoot->keyArray[0] = middleValueFromChild;
		newRoot->pageNoArray[0] = rootPageNum;
		newRoot->pageNoArray[1] = newlyCreatedPageId;
		newRoot->size = 1;
		if(rootIsLeaf) {
			newRoot->level = 1;
		} else {
			newRoot->level = 0;
		}
		rootIsLeaf = false;

		// Update global variable and IndexMetaInfo page appropriately
		Page *meta;
		bufMgr->readPage(file, headerPageNum, meta);
		IndexMetaInfo *metadata = (IndexMetaInfo *)meta;
		metadata->rootPageNo = newPageId;
		rootPageNum = newPageId;

		// Unpin the root and the IndexMetaInfo page
		bufMgr->unPinPage(file, newPageId, true);
		bufMgr->unPinPage(file, headerPageNum, true);
	}
}

// -----------------------------------------------------------------------------
// BTreeIndex::startScan
// -----------------------------------------------------------------------------

void BTreeIndex::startScan(const void* lowValParm,
				   const Operator lowOpParm,
				   const void* highValParm,
				   const Operator highOpParm)
{
	currentPageNum = Page::INVALID_NUMBER;
	if (lowOpParm != GT && lowOpParm != GTE){
		throw BadOpcodesException();
	}
	if (highOpParm != LT && highOpParm != LTE) {
		throw BadOpcodesException();
	}

	//set scanExecuting status to true
	scanExecuting = true;

	//store values
	lowValInt = *((int *)lowValParm);
  	highValInt = *((int *)highValParm);
	lowOp = lowOpParm;
	highOp = highOpParm;

	if (lowValInt > highValInt){
		throw BadScanrangeException();
	}

	currentPageNum = rootPageNum;
	bufMgr->readPage(file, currentPageNum,currentPageData);
	//find leaf node 
	if(!rootIsLeaf) {
		while (true){
			NonLeafNodeInt *inner = (NonLeafNodeInt*) currentPageData;
			bufMgr -> unPinPage(file, currentPageNum,false);

			//find next index of int larger than lower bound
			int index = 0;
			while (index < INTARRAYNONLEAFSIZE && inner->keyArray[index] < lowValInt ){
				index ++; 
			}

			currentPageNum = inner->pageNoArray[index];

			if(inner->level == 1) {
				bufMgr->readPage(file, currentPageNum, currentPageData);
				break;
			}

			bufMgr->readPage(file, currentPageNum, currentPageData);
		}
	}

	//find first record that fits the condition
	
	bool found = false;
	while (!found){
		LeafNodeInt *leaf = (LeafNodeInt*) currentPageData;
		int index = 0;
		
		// if(leaf->ridArray[0].page_number == 0)
		// {
		// 	bufMgr->unPinPage(file, currentPageNum, false);
		// 	throw NoSuchKeyFoundException();
		// }
		
		//loop throw keys in leaf node to find corresponding key
		while (index<INTARRAYLEAFSIZE ){
			int key = leaf->keyArray[index];
			RecordId record = leaf->ridArray[index];

			//this node does not contain more key, break
			if (record.page_number == Page::INVALID_NUMBER){
				break;
			}

			//if excess the range, throw exception
			if((highOp == LT && key >= highValInt) || (highOp == LTE && key >highValInt)){
				bufMgr->unPinPage(file, currentPageNum, false);
				throw NoSuchKeyFoundException();
			}

			//if found then break, and set for the nextEntry
			if ((lowOp == GT && key > lowValInt) || (lowOp == GTE && key >= lowValInt)){
				nextEntry = index;
				found = true;
				break;

			}
			index += 1;


		}

		if(found){
			break;
		}

		//if not found in this node, unpin this page and go to the next leaf node
		bufMgr->unPinPage(file, currentPageNum,false);
		
		//throw exception if no more right subling exist
		if(leaf->rightSibPageNo == Page::INVALID_NUMBER){
			throw NoSuchKeyFoundException();
		}

		//currentPageNum = leaf->rightSibPageNo;
		bufMgr->readPage(file,currentPageNum, currentPageData);
	}
}

// -----------------------------------------------------------------------------
// BTreeIndex::scanNext
// -----------------------------------------------------------------------------

void BTreeIndex::scanNext(RecordId& outRid) 
{
	//throw exception if scan does not begin
	if(!scanExecuting){
		throw ScanNotInitializedException();
	}

	LeafNodeInt *leaf = (LeafNodeInt *)currentPageData;
	int key = leaf->keyArray[nextEntry];
	outRid = leaf->ridArray[nextEntry];

	//throw exception if does not satisfy the condition
	if((highOp == LT && key >= highValInt) || (highOp == LTE && key > highValInt)){
		throw IndexScanCompletedException();
	}

	if(outRid.page_number == Page::INVALID_NUMBER){
		throw IndexScanCompletedException();
	}
	
	//set next entry
	nextEntry ++;
	//if no more key in this node, get next node 
	if(leaf->ridArray[nextEntry].page_number == Page::INVALID_NUMBER || nextEntry >= INTARRAYLEAFSIZE){
		bufMgr -> unPinPage(file,currentPageNum,false);
		//throw exception if no more node
		if(leaf->rightSibPageNo == Page::INVALID_NUMBER){
			throw IndexScanCompletedException();
		}
		currentPageNum = leaf->rightSibPageNo;
		bufMgr->readPage(file, currentPageNum, currentPageData);
		//reset the entry for new node
		nextEntry = 0;
	}
}

// -----------------------------------------------------------------------------
// BTreeIndex::endScan
// -----------------------------------------------------------------------------
//
void BTreeIndex::endScan() 
{
	if(!scanExecuting)
	{
	throw ScanNotInitializedException();
	}
	//set scan state to false
	scanExecuting = false;
	// Unpin page
	if(currentPageNum != Page::INVALID_NUMBER) {
		bufMgr->unPinPage(file, currentPageNum, false);
	}
}

}
