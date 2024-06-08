#include "CharBuffer.h"
#include "Config.h"

void CharBuffer::initialize(char16_t buffer_size)
{
    _buffer = new char[buffer_size];
    _bufferPresence = new char[CHAR_BUFFER_SIZE];    
}

char *CharBuffer::get()
{
    return _buffer;
}

char *CharBuffer::getPresence()
{
    return _bufferPresence;
}

char* CharBuffer::_buffer;
char* CharBuffer::_bufferPresence;