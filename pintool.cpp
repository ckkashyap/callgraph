#include <fstream>
#include <iomanip>
#include <iostream>
#include <string.h>
#include <vector>
#include "pin.H"

#include <intrin.h>

#pragma intrinsic(__rdtsc)

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "trace.out", "Specify filename for trace");

KNOB<string> KnobStartSymbol(KNOB_MODE_WRITEONCE, "pintool",
    "start_symbol", "StartSymbol", "Specify the symbol to start tracing");

KNOB<string> KnobStopSymbol(KNOB_MODE_WRITEONCE, "pintool",
    "stop_symbol", "StopSymbol", "specify the symbol to stop tracing");

PIN_LOCK lock;
INT32 numThreads = 0;
ofstream OutFile;

ADDRINT startSymbolAddress, stopSymbolAddress;
string start_symbol, stop_symbol;
static  TLS_KEY tls_key;

volatile bool guard = false;

#define PADSIZE 56  // 64 byte line size: 64-8

typedef struct _TUP {
  ADDRINT ptr;
  int call;
} TUP;


class thread_data_t
{
public:
  thread_data_t(THREADID tid) : calltrace(vector<TUP*>()) {
    string filename = KnobOutputFile.Value() + ".thread" + decstr(tid) ;
    _ofile.open(filename.c_str());
    if ( ! _ofile )
      {
        cerr << "Error: could not open output file." << endl;
        exit(1);
      }
    _ofile << hex;
  }
  ~thread_data_t() {
    LOG("Destroying thread data\n");
    unsigned int size = calltrace.size();
    LOG("Size = " + decstr(size) + "\n");    
    for (unsigned int i=0;i<size; i++ ){
      TUP *t = calltrace[i];
      _ofile << t->ptr << ", " << t->call << endl;

    }
    LOG("Finished writing\n");    
    _ofile.close();
  }
  ofstream _ofile;
  vector<TUP *> calltrace;
  UINT8 _pad[PADSIZE];
};
thread_data_t* tdata;


//////////////////////////////////////////////



// Holds instruction count for a single procedure
typedef struct RtnCount
{
    string _name;
    string _image;
    ADDRINT _address;
    struct RtnCount * _next;
} RTN_COUNT;



// Linked list of instruction counts for each routine
RTN_COUNT * RtnList = 0;

VOID PIN_FAST_ANALYSIS_CALL startRoutineEnter(UINT64 *counter , THREADID threadid)
{
  if(threadid!=0)return;
  RTN_COUNT *r = reinterpret_cast<RTN_COUNT*>(counter);
  TUP *t = new TUP;
  t->ptr=r->_address;
  t->call=1;
  tdata->calltrace.push_back(t);
  guard=true;
  LOG("Started logging\n");
}

VOID PIN_FAST_ANALYSIS_CALL stopRoutineEnter(UINT64 *counter , THREADID threadid)
{
  if(threadid!=0)return;
  guard=false;
  LOG("Stopped logging\n");
  RTN_COUNT *r = reinterpret_cast<RTN_COUNT*>(counter);
  TUP *t = new TUP;
  t->ptr=r->_address;
  t->call=1;
  tdata->calltrace.push_back(t);
}



// This function is called before every instruction is executed
VOID PIN_FAST_ANALYSIS_CALL routineEnter(UINT64 *counter , THREADID threadid)
{
  if(!guard)return;
  if(threadid!=0)return;

  RTN_COUNT *r = reinterpret_cast<RTN_COUNT*>(counter);
 
  TUP *t = new TUP;
  t->ptr=r->_address;
  t->call=1;
  tdata->calltrace.push_back(t);

}

VOID PIN_FAST_ANALYSIS_CALL routineExit(UINT64 *counter , THREADID threadid)
{
  if(!guard)return;
  if(threadid!=0)return;

  RTN_COUNT *r = reinterpret_cast<RTN_COUNT*>(counter);
 
  TUP *t = new TUP;
  t->ptr=r->_address;
  t->call=0;
  tdata->calltrace.push_back(t);
}


const char * StripPath(const char * path)
{
  const char * file = strrchr(path,'/');
  if (file)
    return file+1;
  else
    return path;
}



VOID Routine(RTN rtn, VOID *v)
{
    
  // Allocate a counter for this routine
  RTN_COUNT * rc = new RTN_COUNT;

  // The RTN goes away when the image is unloaded, so save it now
  // because we need it in the fini
  rc->_name = RTN_Name(rtn);
  rc->_image = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());
  rc->_address = RTN_Address(rtn);
  rc->_next = RtnList;
  RtnList = rc;



  if(!startSymbolAddress && rc->_name == start_symbol) {
    startSymbolAddress = rc->_address;
  }
  if(!stopSymbolAddress && rc->_name == stop_symbol) {
    stopSymbolAddress = rc->_address;
  }


    RTN_Open(rtn);
            
    if (rc->_address == startSymbolAddress || rc->_address == stopSymbolAddress) {
      if (rc->_address == startSymbolAddress) {
	RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)startRoutineEnter, IARG_FAST_ANALYSIS_CALL, IARG_PTR, reinterpret_cast<UINT64*>(rc), IARG_THREAD_ID,  IARG_END);
	LOG("Instrumented start symbol\n");
      } else {
	RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)stopRoutineEnter, IARG_FAST_ANALYSIS_CALL, IARG_PTR, reinterpret_cast<UINT64*>(rc), IARG_THREAD_ID, IARG_END);
	LOG("Instrumented stop symbol\n");
      }
    }else {
      RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)routineEnter, IARG_FAST_ANALYSIS_CALL, IARG_PTR, reinterpret_cast<UINT64*>(rc), IARG_THREAD_ID,  IARG_END);
    }

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

  tdata = new thread_data_t (threadid);

  PIN_SetThreadData(tls_key, tdata, threadid);
}
VOID ThreadFini(THREADID threadid, const CONTEXT *ctxt, INT32 flags, VOID *v)
{
  if(threadid!=0)return;
  delete tdata;
  tdata=NULL;
  PIN_SetThreadData(tls_key, NULL, threadid);
}




// This function is called when the application exits
VOID Fini(INT32 code, VOID *v)
{

  LOG("Finish routine started\n");

  OutFile.open(KnobOutputFile.Value().c_str());
  OutFile << hex;

  for (RTN_COUNT * rc = RtnList; rc; rc = rc->_next) {
    OutFile << rc->_address << "," << rc->_name  << "," << rc->_image << endl;
  }
  OutFile.close();    

  LOG("Finish dumping symbols\n");

  if(tdata) {
    LOG("Thread Was alive\n");
    delete tdata;
  }
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



    start_symbol = KnobStartSymbol.Value();
    stop_symbol = KnobStopSymbol.Value();

    LOG ( "start symbol" + start_symbol + "\n");
    LOG ( "stop symbol = " + stop_symbol + "\n");

    PIN_InitLock(&lock);

    tls_key = PIN_CreateThreadDataKey(0);

    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);

    RTN_AddInstrumentFunction(Routine, 0);


    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();
    
    return 0;
}
