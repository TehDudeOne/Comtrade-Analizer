                                                              
  
                                                                               
                                                                            
                                                                            
#pragma once

#include <windows.h>

#include <string>

namespace analizador::winapp {

                                                                     
                                                                               
                                                               
int runApplication(HINSTANCE instance,
                   int show_command,
                   const std::wstring& initial_path = {});

}                                 
