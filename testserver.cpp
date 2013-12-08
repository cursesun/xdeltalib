#include <iostream>
#include <stdio.h>
#include <time.h>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <set>
#include <list>
#include <string.h>

#ifdef _WIN32
	#include <errno.h>
	#include <io.h>
	#include <direct.h>
	#include <functional>
	#include <hash_map>
#else
	#include <dirent.h>
	#include <memory>
	#include <ext/functional>
    #if !defined (__CXX_11__)
    	#include <ext/hash_map>
    #else
    	#include <unordered_map>
    #endif
    #include <memory.h>
    #include <unistd.h>
    #define _stricmp strcasecmp
#endif
#include <algorithm>
#include <typeinfo>
#include <math.h>

#include "mytypes.h"
#include "digest.h"
#include "buffer.h"
#include "simple_socket.h"
#include "passive_socket.h"
#include "rw.h"
#include "tinythread.h"
#include "rollsum.h"
#include "xdeltalib.h"
#include "platform.h"
#include "reconstruct.h"
#include "inplace.h"
#include "xdelta_server.h"

int files_nr = 0;
xdelta::uint64_t total_file_size = 0;
xdelta::uint64_t trans_file_size = 0;

class my_observer : public xdelta::xdelta_observer
{
public:
	my_observer () : recv_bytes_ (0)
					, send_bytes_ (0)
					, files_nr_ (0)
					, tfilsize_ (0)
	{}
	virtual ~my_observer () {}
	xdelta::uint64_t	recv_bytes_;
	xdelta::uint64_t	send_bytes_;
	int			files_nr_;
	xdelta::uint64_t	tfilsize_;
private:
	virtual void start_hash_stream (const std::string & fname, const xdelta::int32_t blk_len)
	{
		files_nr_++;
	}
	virtual void end_hash_stream (const xdelta::uint64_t filsize)
	{
		tfilsize_ += filsize;
	}

	virtual void end_first_round ()
	{
	}

	virtual void next_round (const xdelta::int32_t blk_len)
	{
	}

	virtual void end_one_round ()
	{
	}

	virtual void on_error (const std::string & errmsg, const int errorno)
	{
	}

	virtual void bytes_send (const xdelta::uint32_t bytes)
	{
		send_bytes_ += bytes;
	}

	virtual void bytes_recv (const xdelta::uint32_t bytes)
	{
		recv_bytes_ += bytes;
	}
};

using xdelta::xdelta_server;
using xdelta::f_local_creator;
using xdelta::file_operator;

void test_multiround (const std::string & path, bool compress)
{
	my_observer mo;
	f_local_creator localop (path);
	file_operator &fop = localop;

	xdelta_server multi (compress);
	multi.set_multiround_size (5 * 1024 * 1024);

	xdelta::uint64_t start = time (0);
	multi.run (fop, mo);
	xdelta::uint64_t end = time (0);

	if (mo.tfilsize_ == 0)
		mo.tfilsize_ = mo.send_bytes_ + mo.recv_bytes_;

	xdelta::uint64_t average = (xdelta::uint64_t)((mo.send_bytes_ + mo.recv_bytes_ + \
					mo.tfilsize_)/(float)(end - start));

	printf ("TIME:\t %d seconds\nSEND:\t%lld bytes\nRECV:\t%lld bytes\n"
		"TOTAL:\t%lld\nRATIO:\t%.05f\nAVERAGE: %lld per second.\n"
			, (int)(end - start),  mo.send_bytes_, mo.recv_bytes_
			, mo.tfilsize_
			, (float)((mo.send_bytes_ + mo.recv_bytes_)/(float)(mo.tfilsize_))
			, average);
}

void test_single_round (const std::string & path, bool compress)
{
	my_observer mo;
	f_local_creator localop (path);
	file_operator &fop = localop;

	xdelta_server server (compress);

	xdelta::uint64_t start = time (0);
	server.run (fop, mo);
	xdelta::uint64_t end = time (0);

	if (mo.tfilsize_ == 0)
		mo.tfilsize_ = mo.send_bytes_ + mo.recv_bytes_;

	xdelta::uint64_t average = (xdelta::uint64_t)((mo.send_bytes_ + mo.recv_bytes_ + \
					mo.tfilsize_)/(float)(end - start));

	printf ("TIME:\t %d seconds\nSEND:\t%lld bytes\nRECV:\t%lld bytes\n"
		"TOTAL:\t%lld\nRATIO:\t%.05f\nAVERAGE: %lld per second.\n"
			, (int)(end - start),  mo.send_bytes_, mo.recv_bytes_
			, mo.tfilsize_
			, (float)((mo.send_bytes_ + mo.recv_bytes_)/(float)(mo.tfilsize_))
			, average);
}

void test_inplace_with_single_round (const std::string & path, bool compress)
{
	my_observer mo;
	f_local_creator localop (path);
	file_operator &fop = localop;

	xdelta_server server (compress);
	server.set_inplace ();

	xdelta::uint64_t start = time (0);
	server.run (fop, mo);
	xdelta::uint64_t end = time (0);

	if (mo.tfilsize_ == 0)
		mo.tfilsize_ = mo.send_bytes_ + mo.recv_bytes_;

	xdelta::uint64_t average = (xdelta::uint64_t)((mo.send_bytes_ + mo.recv_bytes_ + \
					mo.tfilsize_)/(float)(end - start));

	printf ("TIME:\t %d seconds\nSEND:\t%lld bytes\nRECV:\t%lld bytes\n"
		"TOTAL:\t%lld\nRATIO:\t%.05f\nAVERAGE: %lld per second.\n"
			, (int)(end - start),  mo.send_bytes_, mo.recv_bytes_
			, mo.tfilsize_
			, (float)((mo.send_bytes_ + mo.recv_bytes_)/(float)(mo.tfilsize_))
			, average);
}

int main (int argn, char ** argc)
{
	if (argn != 4) {
		return -1;
	}

	try {
		bool compress = _stricmp (argc[3], "z") == 0 ? true : false;
		std::string path = argc[1];
		if (_stricmp (argc[2], "m") == 0) {
			test_multiround (path, compress);
		}
		else if (_stricmp (argc[2], "s") == 0) {
			test_single_round (path, compress);
		}
		else if (_stricmp (argc[2], "i") == 0) {
			test_inplace_with_single_round (path, compress);
		}
		else if (_stricmp (argc[2], "a") == 0) {
			test_multiround (path, compress);
			test_single_round (path, compress);
			test_inplace_with_single_round (path, compress);
		}
		else {
			printf ("Error test type.\n");
			exit (-1);
		}
	}
	catch (xdelta::xdelta_exception &e) {
		printf ("%s\n", e.what ());
#ifdef _WIN32
		system ("pause");
#endif
		return -1;
	}

#ifdef _WIN32
	system ("pause");
#endif
    return 0;
}
