#pragma once

class CharBuffer
{
public:
    static void initialize(char16_t buffer_size);
    static char* get();
    static char* getPresence();

private:
    static char* _buffer;
    static char* _bufferPresence;    
};