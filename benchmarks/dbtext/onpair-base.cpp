#include "compressor/Compressor.hpp"
namespace sgtt::compressor {
std::string Compressor::getFileName()
// Get the file name
{
   return getName();
}
//---------------------------------------------------------------------------
std::string Compressor::getInfo()
// Get additional information
{
   return "{}";
}
//---------------------------------------------------------------------------
void Compressor::printInfo()
// Print some debug information
{
}
//---------------------------------------------------------------------------
}
