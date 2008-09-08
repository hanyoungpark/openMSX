// $Id$

#include "ZlibInflate.hh"
#include "FileException.hh"

namespace openmsx {

ZlibInflate::ZlibInflate(const byte* input, unsigned inputLen)
{
	s.zalloc = 0;
	s.zfree = 0;
	s.opaque = 0;
	s.next_in  = const_cast<byte*>(input);
	s.avail_in = inputLen;
	wasInit = false;
}

ZlibInflate::~ZlibInflate()
{
	if (wasInit) {
		inflateEnd(&s);
	}
}

void ZlibInflate::skip(unsigned num)
{
	for (unsigned i = 0; i < num; ++i) {
		getByte();
	}
}

byte ZlibInflate::getByte()
{
	if (s.avail_in <= 0) {
		throw FileException(
			"Error while decompressing: unexpected end of file.");
	}
	--s.avail_in;
	return *(s.next_in++);
}

unsigned ZlibInflate::get16LE()
{
	return (getByte() << 0) + (getByte() << 8);
}

unsigned ZlibInflate::get32LE()
{
	return (getByte() <<  0) + (getByte() <<  8) +
	       (getByte() << 16) + (getByte() << 24);
}

std::string ZlibInflate::getString(unsigned len)
{
	std::string result;
	for (unsigned i = 0; i < len; ++i) {
		result.push_back(getByte());
	}
	return result;
}

std::string ZlibInflate::getCString()
{
	std::string result;
	while (char c = getByte()) {
		result.push_back(c);
	}
	return result;
}

void ZlibInflate::inflate(std::vector<byte>& output, unsigned sizeHint)
{
	inflateInit2(&s, -MAX_WBITS);
	wasInit = true;

	std::vector<byte> buf;
	buf.resize(sizeHint); // initial buffer size
	while (true) {
		s.next_out = &buf[0] + s.total_out;
		s.avail_out = buf.size() - s.total_out;
		int err = ::inflate(&s, Z_NO_FLUSH);
		if (err == Z_STREAM_END) {
			break;
		}
		if (err != Z_OK) {
			throw FileException("Error decompressing gzip");
		}
		buf.resize(buf.size() * 2); // double buffer size
	}

	// assign actual size (trim excess capacity)
	output.assign(buf.begin(), buf.begin() + s.total_out);
}

} // namespace openmsx
