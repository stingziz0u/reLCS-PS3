#pragma once

#ifdef VALIDATE_SAVE_SIZE
extern int32 _saveBufCount;
#define INITSAVEBUF _saveBufCount = 0;
#define VALIDATESAVEBUF(b) assert(_saveBufCount == b);
#else
#define INITSAVEBUF
#define VALIDATESAVEBUF(b)
#endif

inline void
SkipSaveBuf(uint8 *&buf, int32 skip)
{
	buf += skip;
#ifdef VALIDATE_SAVE_SIZE
	_saveBufCount += skip;
#endif
}

inline void
SkipSaveBuf(uint8*& buf, uint32 &length, int32 skip)
{
	buf += skip;
	length += skip;
#ifdef VALIDATE_SAVE_SIZE
	_saveBufCount += skip;
#endif
}

// The save buffer is packed (int16 next to floats...), so a T in it is often
// not aligned. A plain *(float*)buf becomes lfs/stfs at an odd address, which
// the PPU does not do in hardware (the game thread dies). memcpy makes gcc use
// accesses that are fine at any address; on x86 it compiles to the same thing.
template<typename T>
inline void
ReadSaveBuf(T *out, uint8 *&buf)
{
	memcpy((void*)out, buf, sizeof(T));
	SkipSaveBuf(buf, sizeof(T));
}

template<typename T>
inline void
ReadSaveBuf(T *out, uint8 *&buf, uint32 &length)
{
	memcpy((void*)out, buf, sizeof(T));
	SkipSaveBuf(buf, length, sizeof(T));
}

template<typename T>
inline T *
WriteSaveBuf(uint8 *&buf, const T &value)
{
	T *p = (T*)buf;
	memcpy((void*)buf, (const void*)&value, sizeof(T));
	SkipSaveBuf(buf, sizeof(T));
	return p;
}

template<typename T>
inline T *
WriteSaveBuf(uint8 *&buf, uint32 &length, const T &value)
{
	T *p = (T*)buf;
	memcpy((void*)buf, (const void*)&value, sizeof(T));
	SkipSaveBuf(buf, length, sizeof(T));
	return p;
}

#ifdef COMPATIBLE_SAVES
inline void
ZeroSaveBuf(uint8 *&buf, uint32 length)
{
	memset(buf, 0, length);
	SkipSaveBuf(buf, length);
}
#endif

#define SAVE_HEADER_SIZE (4*sizeof(char)+sizeof(uint32))

#define WriteSaveHeader(buf,a,b,c,d,size) \
	WriteSaveBuf(buf, a);\
	WriteSaveBuf(buf, b);\
	WriteSaveBuf(buf, c);\
	WriteSaveBuf(buf, d);\
	WriteSaveBuf<uint32>(buf, size);

#define WriteSaveHeaderWithLength(buf,len,a,b,c,d,size) \
	WriteSaveBuf(buf, len, a);\
	WriteSaveBuf(buf, len, b);\
	WriteSaveBuf(buf, len, c);\
	WriteSaveBuf(buf, len, d);\
	WriteSaveBuf(buf, len, (uint32)(size));

#ifdef VALIDATE_SAVE_SIZE
#define CheckSaveHeader(buf, a, b, c, d, size) do { \
	char _c; uint32 _size;\
	ReadSaveBuf(&_c, buf);\
	assert(_c == a);\
	ReadSaveBuf(&_c, buf);\
	assert(_c == b);\
	ReadSaveBuf(&_c, buf);\
	assert(_c == c);\
	ReadSaveBuf(&_c, buf);\
	assert(_c == d);\
	ReadSaveBuf(&_size, buf);\
	assert(_size == size);\
	} while(0)

#define CheckSaveHeaderWithLength(buf,len,a,b,c,d,size) do { \
	char _c; uint32 _size;\
	ReadSaveBuf(&_c, buf, len);\
	assert(_c == a);\
	ReadSaveBuf(&_c, buf, len);\
	assert(_c == b);\
	ReadSaveBuf(&_c, buf, len);\
	assert(_c == c);\
	ReadSaveBuf(&_c, buf, len);\
	assert(_c == d);\
	ReadSaveBuf(&_size, buf, len);\
	assert(_size == size);\
	} while(0)
#else
#define CheckSaveHeader(buf, a, b, c, d, size) SkipSaveBuf(buf, 8);
#define CheckSaveHeaderWithLength(buf, len, a, b, c, d, size) SkipSaveBuf(buf, 8);
#endif
