#include <iostream>
#include <fstream>
#include <regex>
#include <unordered_map>

using SymbolTable = std::unordered_map<std::string, std::string>;

SymbolTable getSymbolTable(std::string symbolFileName) {
  std::ifstream traceFile(symbolFileName);
  std::regex regularExpression ("(.*?);(.*?);(.*)");
  SymbolTable table {};
  char buffer[1024];
  while((traceFile.getline(buffer,1024)) && !traceFile.eof()) {
    std::string line {buffer};
    std::smatch sm {};
    if(std::regex_match(line, sm, regularExpression)) {
      table[sm[1]] = sm[2];
    }else {
      std::cout << line <<std::endl;
      break;
    }
  }
  return table;
}

void decorateTrace(std::string traceFileName, SymbolTable table) {
  std::ifstream traceFile(traceFileName);
  std::regex regularExpression ("(.*);(.*)");
  auto tabPrint = [] (int n) {
    for(int i=0;i<n;i++) std::cout << " ";
  };
  int tabCount {0};
  for(std::string line {}; (traceFile >> line) && !traceFile.eof(); ) {
    std::smatch sm {};
    if(std::regex_match(line, sm, regularExpression)) {
      std::string symbol {"NOT FOUND"};
      try {
	symbol = table.at(sm[1]);
      }catch(...){}

      if(sm[2] == "1") {
	tabPrint(tabCount);
	std::cout << symbol << " " << sm[2] << std::endl;
	tabCount++;
      }else {
	tabCount--;	
	tabPrint(tabCount);
	std::cout << symbol << " " << sm[2] << std::endl;
      }
    }else break;    
  }
}




int main(int argc, char *argv[]) {
  std::string symbolFileName {argv[1]};
  std::string traceFileName {symbolFileName + ".thread0"};
  auto t = getSymbolTable(argv[1]);
  decorateTrace(traceFileName, t);
  return 0;
}

