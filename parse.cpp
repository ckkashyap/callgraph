#include <iostream>
#include <fstream>
#include <regex>
#include <unordered_map>


using SymbolTable = std::unordered_map<std::string, std::string>;



SymbolTable getSymbolTable(std::string traceFileName) {
  std::ifstream traceFile(traceFileName);
  std::regex regularExpression ("(.*?);(.*?);(.*)");
  SymbolTable table {};
  for(std::string line {}; (traceFile >> line) && !traceFile.eof(); ) {    
    std::smatch sm {};
    if(std::regex_match(line, sm, regularExpression)) {
      table[sm[1]] = sm[2];
    }else break;
  }
  return table;
}



int main(int argc, char *argv[]) {  
  auto t = getSymbolTable(argv[1]);
  for(auto k : t) {
    std::cout << k.first << std::endl;
  }
  return 0;
}

