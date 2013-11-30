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

PIN_LOCK lock;
INT32 numThreads = 0;
ofstream OutFile;

#define PADSIZE 56  // 64 byte line size: 64-8

typedef struct _TUP {
  ADDRINT ptr;
  UINT64 i;
  int e;
} TUP;


class thread_data_t
{
  public:
    thread_data_t() : _count(0) {}
    UINT64 _count;
  vector<TUP*> calltrace;
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

  RTN_COUNT *r = reinterpret_cast<RTN_COUNT*>(counter);
  TUP *t = new TUP;
  t->ptr=r->_address;
  t->i=__rdtsc();
  t->e=1;

  thread_data_t* tdata = get_tls(threadid);
  tdata->calltrace.push_back(t);


  ctr++;
}

VOID PIN_FAST_ANALYSIS_CALL routineExit(UINT64 *counter , THREADID threadid)
{
  RTN_COUNT *r = reinterpret_cast<RTN_COUNT*>(counter);

  TUP *t = new TUP;
  t->ptr=r->_address;
  t->i=__rdtsc();
  t->e=0;

  thread_data_t* tdata = get_tls(threadid);
  tdata->calltrace.push_back(t);

  ctr++;
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

    rc->_next = RtnList;
    RtnList = rc;
            
    RTN_Open(rtn);
            
    // Insert a call at the entry point of a routine to increment the call count
    RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)routineEnter, IARG_FAST_ANALYSIS_CALL, IARG_PTR, reinterpret_cast<UINT64*>(rc), IARG_THREAD_ID,  IARG_END);
    RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR)routineExit, IARG_FAST_ANALYSIS_CALL, IARG_PTR, reinterpret_cast<UINT64*>(rc), IARG_THREAD_ID, IARG_END);
    
    RTN_Close(rtn);
}


/////////////////////////////////////////////





VOID PIN_FAST_ANALYSIS_CALL docount(UINT32 c, THREADID threadid)
{
    thread_data_t* tdata = get_tls(threadid);
    tdata->_count += c;
}

VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    PIN_GetLock(&lock, threadid+1);
    numThreads++;
    PIN_ReleaseLock(&lock);

    thread_data_t* tdata = new thread_data_t;

    PIN_SetThreadData(tls_key, tdata, threadid);
}

VOID Trace(TRACE trace, VOID *v)
{
    // Visit every basic block  in the trace
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        // Insert a call to docount for every bbl, passing the number of instructions.
        
        BBL_InsertCall(bbl, IPOINT_ANYWHERE, (AFUNPTR)docount, IARG_FAST_ANALYSIS_CALL,
                       IARG_UINT32, BBL_NumIns(bbl), IARG_THREAD_ID, IARG_END);
    }
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{

  for (RTN_COUNT * rc = RtnList; rc; rc = rc->_next)
    {
      OutFile << hex << rc->_address << "," << rc->_name  << "," << rc->_image << endl;
      }



    // Write to a file since cout and cerr maybe closed by the application
    OutFile << "Total number of threads = " << numThreads << endl;
    
    for (INT32 t=0; t<numThreads; t++)
    {
      OutFile << "Thread #" << decstr(t) << endl;
      thread_data_t* tdata = get_tls(t);

      vector<TUP*> calltrace = tdata->calltrace;
      int size = calltrace.size();

      
      for (int i=0;i<size;i++){
	OutFile << hex << calltrace[i]->ptr << "," << calltrace[i]->e << "," << calltrace[i]->i << endl;
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

    PIN_InitLock(&lock);

    tls_key = PIN_CreateThreadDataKey(0);

    PIN_AddThreadStartFunction(ThreadStart, 0);

    //TRACE_AddInstrumentFunction(Trace, 0);
    RTN_AddInstrumentFunction(Routine, 0);


    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    
    return 0;
}
