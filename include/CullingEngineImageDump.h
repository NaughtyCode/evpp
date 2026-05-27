#pragma once

#include <string>

bool DumpOccluderOccludeeColorImage(const std::string& filename,
                                    unsigned char* inputBuffer,
                                    unsigned int width, unsigned int height);

void DumpGrayImage(const std::string& filename,
                   const unsigned char* inputBuffer, unsigned int width,
                   unsigned int height);

void DumpImageMOC(const std::string& filename, const unsigned char* inputBuffer,
                  unsigned int width, unsigned int height);
