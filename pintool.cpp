#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <vector>
#include "pin.H"

#include <intrin.h>

#pragma intrinsic(__rdtsc)

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "trace.out", "specify output file name");
KNOB<string> StartSymbol(KNOB_MODE_WRITEONCE, "pintool",
    "s", "StartSymbol", "specify the symbol to start tracing");
KNOB<string> StopSymbol(KNOB_MODE_WRITEONCE, "pintool",
    "e", "StopSymbol", "specify the symbol to stop tracing");
KNOB<unsigned int> BufSize(KNOB_MODE_WRITEONCE, "pintool",
    "z", "BufSize", "BufferSize");



PIN_LOCK lock;
INT32 numThreads = 0;
ofstream OutFile;

ADDRINT startSymbolAddress, stopSymbolAddress;

volatile bool guard = false;

#define PADSIZE 56  // 64 byte line size: 64-8
unsigned int BUFSIZE;

typedef struct _TUP {
  ADDRINT ptr;
  UINT64 i;
  int e;
} TUP;


class thread_data_t
{
public:
  thread_data_t(THREADID tid) : _count(0), calltrace(vector<TUP>(BUFSIZE)), valid(true)  {
    string filename = KnobOutputFile.Value() + "." + decstr(tid);
    _ofile.open(filename.c_str());
    if ( ! _ofile )
      {
        cerr << "Error: could not open output file." << endl;
        exit(1);
      }
    _ofile << hex;
    _count = 0;
    totalCount=0;
  }
  ~thread_data_t() {
    for(int i=0;i<_count;i++) {
      _ofile << calltrace[i].ptr << "," << calltrace[i].e << "," << calltrace[i].i << endl;
    }

    _ofile.close();
    valid=false;
  }
  bool valid;
  ofstream _ofile;
  UINT64 _count;
  UINT64 totalCount;
  vector<TUP> calltrace;
  UINT8 _pad[PADSIZE];
};


static  TLS_KEY tls_key;


thread_data_t* get_tls(THREADID threadid)
{
    thread_data_t* tdata = 
          static_cast<thread_data_t*>(PIN_GetThreadData(tls_key, threadid));
    return tdata;
}





//////////////////////////////////////////////



// Holds instruction count for a single procedure
typedef struct RtnCount
{
    string _name;
    string _image;
    ADDRINT _address;
    struct RtnCount * _next;
} RTN_COUNT;


UINT64 ctr;


// Linked list of instruction counts for each routine
RTN_COUNT * RtnList = 0;

// This function is called before every instruction is executed
VOID PIN_FAST_ANALYSIS_CALL routineEnter(UINT64 *counter , THREADID threadid)
{
  if(threadid!=0)return;

  RTN_COUNT *r = reinterpret_cast<RTN_COUNT*>(counter);
  

  if(r->_address==startSymbolAddress)guard=true;

  if(!guard)return;

  thread_data_t* tdata = get_tls(threadid);  

  UINT64 index = tdata->_count;
  tdata->calltrace[index].ptr=r->_address;
  tdata->calltrace[index].i=__rdtsc();
  tdata->calltrace[index].e=1;
  index++;
  if(index == BUFSIZE) {
    index=0;
    for(int i=0;i<BUFSIZE;i++) {
      tdata->_ofile << tdata->calltrace[i].ptr << "," << tdata->calltrace[i].e << "," << tdata->calltrace[i].i << endl;
    }
  }
  tdata->_count=index;
}

VOID PIN_FAST_ANALYSIS_CALL routineExit(UINT64 *counter , THREADID threadid)
{
  if(threadid!=0)return;
  RTN_COUNT *r = reinterpret_cast<RTN_COUNT*>(counter);

  if(!guard)return;

  if(r->_address==stopSymbolAddress)guard=false;

  thread_data_t* tdata = get_tls(threadid);

  UINT64 index = tdata->_count;
  tdata->calltrace[index].ptr=r->_address;
  tdata->calltrace[index].i=__rdtsc();
  tdata->calltrace[index].e=0;
  index++;
  if(index == BUFSIZE) {
    index=0;
    for(int i=0;i<BUFSIZE;i++) {
      tdata->_ofile << tdata->calltrace[i].ptr << "," << tdata->calltrace[i].e << "," << tdata->calltrace[i].i << endl;
    }
  }
  tdata->_count=index;
}

    
const char * StripPath(const char * path)
{
    const char * file = strrchr(path,'/');
    if (file)
        return file+1;
    else
        return path;
}


char *caps(char *in) {
  int i=0;
  while(1) {
    if(!in[i])break;
    if(in[i]>='a'&&in[i]<='z')in[i]-=32;
    i++;
  }
  return in;
}


//// Pin calls this function every time a new rtn is executed
VOID Routine(RTN rtn, VOID *v)
{
  char *filter[] = {
    "C:\\WINDOWS"
  };
  char *image = const_cast<char *>(StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str()));
  char imageCaps[500];
  strcpy(imageCaps, image);
  caps(imageCaps);
  

  int n = sizeof(filter)/sizeof(char*);
  for(int i=0;i<n;i++) {
    if(strstr(imageCaps, filter[i])) {
      return;
    }
  }

    
    // Allocate a counter for this routine
    RTN_COUNT * rc = new RTN_COUNT;

    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    rc->_name = RTN_Name(rtn);
    rc->_image = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());
    rc->_address = RTN_Address(rtn);


    if(0==strcmp(RTN_Name(rtn).c_str(), StartSymbol.Value().c_str()))
      startSymbolAddress = RTN_Address(rtn);


    if(0==strcmp(RTN_Name(rtn).c_str(), StopSymbol.Value().c_str()))
      stopSymbolAddress = RTN_Address(rtn);

      

    rc->_next = RtnList;
    RtnList = rc;
            
    RTN_Open(rtn);
            
    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)routineEnter, IARG_FAST_ANALYSIS_CALL, IARG_PTR, reinterpret_cast<UINT64*>(rc), IARG_THREAD_ID,  IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)routineExit, IARG_FAST_ANALYSIS_CALL, IARG_PTR, reinterpret_cast<UINT64*>(rc), IARG_THREAD_ID, IARG_END);
    
    RTN_Close(rtn);
}


/////////////////////////////////////////////






VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
  if(threadid!=0)return;
  PIN_GetLock(&lock, threadid+1);
  numThreads++;
  PIN_ReleaseLock(&lock);
    

  thread_data_t* tdata = new thread_data_t (threadid);

  PIN_SetThreadData(tls_key, tdata, threadid);
}
VOID ThreadFini(THREADID threadid, const CONTEXT *ctxt, INT32 flags, VOID *v)
{
  if(threadid!=0)return;
  thread_data_t* tdata = get_tls(threadid);
  delete tdata;
  PIN_SetThreadData(tls_key, NULL, threadid);
}




// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{

  LOG("Finish routine started\n");
  OutFile << hex;

  for (RTN_COUNT * rc = RtnList; rc; rc = rc->_next)
    {
      OutFile << rc->_address << "," << rc->_name  << "," << rc->_image << endl;
      }

  LOG("Finish dumping symbols\n");

    // Write to a file since cout and cerr maybe closed by the application
    OutFile << "Total number of threads = " << numThreads << endl;
    
    for (INT32 t=0; t<numThreads; t++)
    {
      thread_data_t* tdata = get_tls(t);
      if(tdata) {
	OutFile << "Thread " << t << " Was alive" << endl;
	delete tdata;
      }else {
	OutFile << "Thread " << t << " Was dead" << endl;
      }
    }


    OutFile.close();

}

INT32 Usage()
{
    cerr << "This tool counts the number of dynamic instructions executed" << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

int main(int argc, char * argv[])
{
    // Initialize pin
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) return Usage();

    OutFile.open(KnobOutputFile.Value().c_str());
    BUFSIZE = BufSize.Value();
    LOG ("Size = " + decstr(BUFSIZE) + "\n");

    PIN_InitLock(&lock);

    tls_key = PIN_CreateThreadDataKey(0);

    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    RTN_AddInstrumentFunction(Routine, 0);


    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    
    return 0;
}
