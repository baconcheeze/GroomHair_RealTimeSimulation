#include "OrientationFixtures.h"
#include <iostream>
int main(){try{std::cout<<hair::validateMath().json()<<'\n';return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
