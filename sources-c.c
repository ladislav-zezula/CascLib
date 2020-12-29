#include "src\jenkins\lookup3.c"

#pragma warning(disable:4005)       // warning C4005: 'DO1' : macro redefinition
#pragma warning(disable:4242)       // deflate.c(1693) : warning C4242: '=' : conversion from 'unsigned int' to 'Bytef', possible loss of data
#include "src\zlib\adler32.c"
#include "src\zlib\crc32.c"
#include "src\zlib\deflate.c"
#include "src\zlib\trees.c"
#include "src\zlib\zutil.c"

#undef COPY                         // Conflicting definition
#include "src\zlib\inffast.c"
#include "src\zlib\inflate.c"
#include "src\zlib\inftrees.c"
