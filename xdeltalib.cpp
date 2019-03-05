/*
 * Copyright (C) 2013- yeyouqun@163.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, visit the http://fsf.org website.
 */

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
	#include <windows.h>
	#include <errno.h>
	#include <io.h>
	#include <share.h>
	#define _SILENCE_STDEXT_HASH_DEPRECATION_WARNINGS
	#include <hash_map>
	#include <functional>
#else
    #if !defined (__CXX_11__)
    	#include <ext/hash_map>
    #else
    	#include <unordered_map>
    #endif
	#include <sys/types.h>
    #include <unistd.h>
    #include <sys/stat.h>
	#include <fcntl.h>
	#include <ext/functional>
	#include <memory.h>
	#include <stdio.h>
#endif
#include <set>
#include <string>
#include <iterator>
#include <list>
#include <algorithm>
#include <vector>
#include <math.h>
#include <assert.h>

#include "mytypes.h"
#include "md4.h"
#include "rw.h"
#include "rollsum.h"
#include "buffer.h"
#include "xdeltalib.h"
#include "platform.h"

namespace xdelta {
//
//
// Our implementations.
//

static void free_hash_set (const std::pair<uint32_t, std::set<slow_hash> *> & p)
{
	delete p.second;
}

hash_table::~hash_table ()
{
	clear ();
}

void hash_table::clear ()
{
	std::for_each (hash_table_.begin (), hash_table_.end (), free_hash_set);
	hash_table_.clear ();
}

const slow_hash * hash_table::find_block (const uint32_t fhash
										, const uchar_t * buf
										, const uint32_t len) const
{
	chit_t pos;
	if ((pos = hash_table_.find (fhash)) == hash_table_.end ())
		return 0;

	slow_hash bsh;
	get_slow_hash (buf, len, bsh.hash);
	bsh.tpos.index = -1; // not used.

	std::set<slow_hash>::const_iterator hashpos;
	if ((hashpos = pos->second->find (bsh)) == pos->second->end ())
		return 0;

	return &*hashpos;
}

void hash_table::add_block (const uint32_t fhash, const slow_hash & shash)
{
	hash_iter pos;
	if ((pos = hash_table_.find (fhash)) == hash_table_.end ()) {
		pos = hash_table_.insert (std::make_pair (fhash, new std::set<slow_hash> ())).first;
	}
	
	pos->second->insert (shash);
}

/// \fn read_and_hash()
/// \brief
/// ������ӿ�ʵ�ּ���졢����ϣ��
void read_and_hash (file_reader & reader
							, hasher_stream & stream
							, uint64_t to_read_bytes
							, const int32_t blk_len
							, uint64_t t_offset
							, rs_mdfour_t * pctx)
{
	//
	// read huge block one time and calc hash block after block length of f_blk_len;
	//
	uint32_t buflen;
	char_buffer<uchar_t> buf (XDELTA_BUFFER_LEN);

	uint64_t index = 0;
	uchar_t * rdbuf = buf.begin ();
	buflen = (uint32_t)(to_read_bytes > XDELTA_BUFFER_LEN ? XDELTA_BUFFER_LEN : to_read_bytes);

	while (to_read_bytes > 0) {
		//
		// to_read_bytes ��Ӧ�˻����Զ�ȡ�����ݣ��������һ������ ��ȡ�� buflen ��С�����ݡ�������ܹ���
		// ���п��ܵ���������ѭ�������������ڵȴ������� Bug �������»ᷢ����
		//
		uchar_t * endbuf = rdbuf;
		while (buflen > 0) {
			int size = reader.read_file (endbuf, buflen);
			if (size <= 0) {
				std::string errmsg = "Can't not read file or pipe.";
				THROW_XDELTA_EXCEPTION (errmsg);
			}

			if (pctx != 0)
				rs_mdfour_update(pctx, rdbuf, size);

			to_read_bytes -= size;
			endbuf += size;
			buflen -= size;
		}

		rdbuf = buf.begin ();
		while ((int32_t)(endbuf - rdbuf) >= blk_len) {
			uint32_t fhash = rolling_hasher::hash (rdbuf, blk_len);
			struct slow_hash bsh;
			bsh.tpos.index = index;
			bsh.tpos.t_offset = t_offset;
			get_slow_hash (rdbuf, blk_len, bsh.hash);
			stream.add_block (fhash, bsh);
			++index;
			rdbuf += blk_len;
		}

		uint32_t remain = (int32_t)(endbuf - rdbuf);
		if (remain > 0)
			memmove (buf.begin (), rdbuf, remain);

		rdbuf = buf.begin () + remain;
		buflen = XDELTA_BUFFER_LEN - remain;
		buflen = (uint32_t)(to_read_bytes > buflen ? buflen : to_read_bytes);
	}
}

//
// used the hash_table object receive from remote host 
// and send a hash stream to the remote host.
//
void hash_table::hash_it (file_reader & reader, hasher_stream & stream) const
{
	rs_mdfour_t ctx;
	rs_mdfour_begin(&ctx);
	uint64_t filsize = 0;
	try {
		int32_t f_blk_len = 0;
		if (reader.exist_file ()) {
			try {
				reader.open_file (); // sometimes this will throw.
				filsize = reader.get_file_size ();
				f_blk_len = get_xdelta_block_size (filsize);
			}
			catch (...) {
				stream.start_hash_stream (reader.get_fname (), 0);
				throw;
			}
			stream.start_hash_stream (reader.get_fname (), f_blk_len);
		}
		else {
			std::string mesg = fmt_string ("File %s not exists."
				, reader.get_fname ().c_str ());
			stream.start_hash_stream (reader.get_fname (), 0);
			stream.on_error (mesg, ENOENT);
			goto end;
		}
		read_and_hash (reader, stream, filsize, f_blk_len, 0, &ctx);
	}
	catch (xdelta_exception &e) {
		stream.on_error (e.what (), e.get_errno ());
	}

end:
	uchar_t file_hash[DIGEST_BYTES];
	memset (file_hash, 0, sizeof (file_hash));
	rs_mdfour_result (&ctx, file_hash);

	reader.close_file ();
	stream.end_hash_stream (file_hash, filsize);
	return;
}

void multiround_hash_table::hash_it (file_reader & reader, hasher_stream & stream) const
{
	std::set<hole_t> holes;
	stream.set_holes (&holes);
	int32_t blk_len = _hasher_first_round(reader, stream, holes);
	//
	// -1, means file is equal, so
	// no need to goto the next round. 
	//
	if (blk_len != -1) {
		blk_len /= multiround_base ();
		while (blk_len > minimal_multiround_block ()) {
			_next_round (reader, stream, holes, blk_len);
			blk_len /= multiround_base ();
		}
	}
	uchar_t file_hash[DIGEST_BYTES];
	memset (file_hash, 0, DIGEST_BYTES);
	uint64_t filsize = reader.get_file_size ();
	reader.close_file ();
	stream.end_hash_stream (file_hash, filsize);
}


int32_t multiround_hash_table::_hasher_first_round(file_reader & reader
										, hasher_stream & stream
										, std::set<hole_t> & holes) const
{
	if (!holes.empty ()) {
		std::string errmsg = fmt_string ("Holes must be empty.");
		THROW_XDELTA_EXCEPTION (errmsg);
	}

	hole_t hole;
	hole.offset = 0;
	bool round = true;
	try {
		reader.open_file (); // sometimes this will throw.
		hole.length = reader.get_file_size ();
		holes.insert (hole);
	}
	catch (...) {
		round = false;
	}

	if (!round) {
		hash_table::hash_it (reader, stream);
		return -1;
	}

	int32_t blk_len = (uint32_t)(hole.length / multiround_base ());
	if (blk_len > MULTIROUND_MAX_BLOCK_SIZE)
		blk_len = MULTIROUND_MAX_BLOCK_SIZE;
	if (blk_len < XDELTA_BLOCK_SIZE)
		blk_len = XDELTA_BLOCK_SIZE;

	stream.start_hash_stream (reader.get_fname (), blk_len);
	rs_mdfour_t ctx;
	rs_mdfour_begin(&ctx);

	read_and_hash (reader, stream, hole.length, blk_len, hole.offset, &ctx);
	
	uchar_t file_hash[DIGEST_BYTES];
	memset (&file_hash, 0, sizeof (file_hash));
	rs_mdfour_result (&ctx, file_hash);

	bool neednextround = stream.end_first_round (file_hash);
	return (neednextround ? blk_len : -1);
}

void multiround_hash_table::_next_round (file_reader & reader
										, hasher_stream & stream
										, std::set<hole_t> & holes
										, const int32_t blk_len) const
{
	if (holes.empty ()) {
		std::string errmsg = fmt_string ("Holes mustn't be empty.");
		THROW_XDELTA_EXCEPTION (errmsg);
	}

	try {
		stream.next_round (blk_len);
		typedef std::set<hole_t>::const_iterator cit_t;

		for (cit_t begin = holes.begin (); begin != holes.end (); ++begin) {
			const hole_t & hole = *begin;
			if (hole.length < (uint32_t)blk_len)
				continue;

			uint64_t offset = reader.seek_file (hole.offset, FILE_BEGIN);
			if (offset != hole.offset) {
				std::string errmsg = fmt_string ("Can't seek file %s(%s)."
					, reader.get_fname ().c_str (), error_msg ().c_str ());
				THROW_XDELTA_EXCEPTION (errmsg);
			}

			read_and_hash (reader, stream, hole.length, blk_len, hole.offset, 0);
		}
		stream.end_one_round ();
	}
	catch (xdelta_exception &e) {
		stream.on_error (e.what (), e.get_errno ());
	}
}

/////////////////////////////////////////////////////////
// class xdelta_hash_table implementation.
xdelta_hash_table::xdelta_hash_table (const hash_table & table
								, file_reader & reader
								, const int f_blk_len)
						: hash_table_ (table)
						, reader_ (reader)
						, f_blk_len_ (f_blk_len)
{
}

xdelta_hash_table::~xdelta_hash_table ()
{
}

void split_hole (std::set<hole_t> & holeset, const hole_t & hole)
{
	typedef std::set<hole_t>::iterator it_t;
	hole_t findhole;
	findhole.offset = hole.offset;
	findhole.length = 0;

	it_t pos = holeset.find (findhole);
	if (pos != holeset.end ()) {
		const hole_t bighole = *pos;
		if (bighole.offset <= hole.offset && 
			(bighole.offset + bighole.length) >= (hole.offset + hole.length)) {
				// split to one or more hole, like this
				// |--------------------------------------|
				// |---------| added block |--------------|
				holeset.erase (pos);
				if (bighole.offset < hole.offset) {
					hole_t newhole;
					newhole.offset = bighole.offset;
					newhole.length = hole.offset - bighole.offset;
					holeset.insert (newhole);
				}

				uint64_t bigend = bighole.offset + bighole.length,
					     holeend = hole.offset + hole.length;
				if (bigend > holeend) {
					hole_t newhole;
					newhole.offset = hole.offset + hole.length;
					newhole.length = bigend - holeend;
					holeset.insert (newhole);
				}

				return;
		}
	}

	BUG("hole must be exists");
}

/// \fn read_and_delta()
/// \brief
/// ������ӿ�ʵ�ֲ������ݵ���ȡ������ط��������㷨�ĺ��ģ�������
/// ���־���������������ܱ��֡�Ӧ�þ��������ִ��Ч�ʣ���֪���ɷ����
/// ����Ĳ������㷽����
void read_and_delta (file_reader & reader
					, xdelta_stream & stream
					, const hash_table & hashes
					, std::set<hole_t> & hole_set
					, const int blk_len
					, bool need_split_hole)
{
	bool adddiff = !need_split_hole;
	char_buffer<uchar_t> buf (XDELTA_BUFFER_LEN);
	typedef std::set<hole_t>::iterator it_t;
	std::list<hole_t> holes2remove;

	for (it_t begin = hole_set.begin (); begin != hole_set.end (); ++begin) {
		const hole_t & hole = *begin;
		uint64_t offset = reader.seek_file(hole.offset, FILE_BEGIN);
		if (offset != hole.offset) {
			std::string errmsg = fmt_string("Can't seek file %s(%s)."
				, reader.get_fname().c_str(), error_msg().c_str());
			THROW_XDELTA_EXCEPTION(errmsg);
		}

		uint32_t to_read_bytes = (uint32_t)hole.length;
		uchar_t * rdbuf = buf.begin();
		uchar_t * endbuf = rdbuf, *sentrybuf = rdbuf;

		rolling_hasher hasher;
		bool newhash = true;
		int32_t remain = 0;
		uchar_t outchar = 0;
		while (true) {
			if (remain < blk_len) {
				if (to_read_bytes == 0) {
					uint32_t slipsize = (uint32_t)(endbuf - sentrybuf);
					if (slipsize > 0 && adddiff)
						stream.add_block(sentrybuf, slipsize, offset);
					break;
				}
				else {
					uint32_t slipsize = (uint32_t)(rdbuf - sentrybuf);
					if (slipsize > 0) {
						if (adddiff)
							stream.add_block(sentrybuf, slipsize, offset);
						offset += slipsize;
					}

					if (remain > 0)
						memmove(buf.begin(), rdbuf, remain);

					sentrybuf = buf.begin();
					uint32_t buflen = XDELTA_BUFFER_LEN - remain;
					buflen = to_read_bytes > buflen ? buflen : to_read_bytes;
					rdbuf = sentrybuf;
					endbuf = sentrybuf + remain;
					//
					// ��ȡ�ļ�ʱ����� reader ���ļ�����һ�ξͿɶ��꣬����ǹܵ����������Ҫ��β��ܽ�����ȡ��ɣ�
					// ���� hole �Ĵ�С����ӳ�����ݵĴ�С���� to_read_bytes ��Ӧ�˻����Զ�ȡ�����ݣ��������һ������
					// ��ȡ�� buflen ��С�����ݡ�������ܹ������п��ܵ���������ѭ�������������ڵȴ������� Bug ������
					// �»ᷢ����
					//
					while (buflen > 0) {
						int size = reader.read_file(sentrybuf + remain, buflen);
						if (size <= 0) {
							std::string errmsg = "Can't not read file or pipe.";
							THROW_XDELTA_EXCEPTION (errmsg);
						}
						to_read_bytes -= size;
						buflen -= size;
						endbuf += size;
						remain += size;
					}
					continue;
				}
			}
			else {
				if (newhash) {
					hasher.eat_hash(rdbuf, blk_len);
					newhash = false;
				}
				else
					hasher.update(outchar, *(rdbuf + blk_len - 1));
			}

			const slow_hash * bsh = hashes.find_block(hasher.hash_value()
													, rdbuf, blk_len);
			if (bsh) {
				// a match was found.
				uint32_t slipsize = (uint32_t)(rdbuf - sentrybuf);
				if (slipsize > 0) {
					if (adddiff)
						stream.add_block(sentrybuf, slipsize, offset);

					offset += slipsize;
				}

				stream.add_block(bsh->tpos, blk_len, offset);
				if (need_split_hole) {
					hole_t newhole;
					newhole.offset = offset;
					newhole.length = blk_len;
					holes2remove.push_back(newhole);
				}

				rdbuf += blk_len;
				offset += blk_len;
				remain -= blk_len;
				sentrybuf = rdbuf;
				newhash = true;
			}
			else {
				// slip the window by one bytes which size is blk_len.
				outchar = *rdbuf++;
				--remain;
			}
		}
	}

	if (need_split_hole) {
		typedef std::list<hole_t>::iterator it_t;
		for (it_t begin = holes2remove.begin (); begin != holes2remove.end (); ++begin)
			split_hole (hole_set, *begin);
	}
	return;
}

void xdelta_hash_table::xdelta_it (xdelta_stream & stream)
{
	file_reader & reader = reader_;
	const int f_blk_len = f_blk_len_;

	char_buffer<uchar_t> buf (XDELTA_BUFFER_LEN);
	uint64_t filsize = 0;
	stream.start_hash_stream (reader.get_fname (), f_blk_len);
	try {
		reader.open_file ();
		filsize = reader.get_file_size ();
		if (hash_table_.empty ()) {
			// If dest has no such file or its size  is less than
			// one block, hash_table_ will be empty, so trans all of the file content directly.
			uint64_t offset = 0;
			uint32_t size;
			while ((size = reader.read_file (buf.begin (), XDELTA_BUFFER_LEN)) > 0) {
				stream.add_block (buf.begin (), size, offset);
				offset += size;
			}

			goto end;
		}
	
		hole_t hole;
		hole.offset = 0;
		hole.length = reader.get_file_size ();
		std::set<hole_t> hole_set;
		hole_set.insert (hole);
		read_and_delta (reader, stream, hash_table_, hole_set, f_blk_len, false);
	}
	catch (xdelta_exception &e) {
		stream.on_error (e.what (), e.get_errno ());
	}

end:
	reader.close_file ();
	stream.end_hash_stream (filsize);
	return;
}

////////////////////////////////////////////////////////////////////////////
static uint32_t xdelta_sum_block_size (const uint64_t filesize)
{
	double blk_size = log ((double)filesize)/log ((double)2);
	blk_size *= pow ((double)filesize, 1.0/3);
	uint32_t i_blk_size = (uint32_t)(blk_size);

	if (i_blk_size < XDELTA_BLOCK_SIZE)
		i_blk_size = XDELTA_BLOCK_SIZE;
	else if (i_blk_size > MAX_XDELTA_BLOCK_BYTES)
		i_blk_size = MAX_XDELTA_BLOCK_BYTES;
	else {
		/// ����ʹ���С���ļ���С���룬�������һ��ĵ�����ʣ�µľ��٣�������΢����
		/// ��Դ�˴�����ͬ���ݿ�Ŀ��ܣ��Ӷ��������ݴ�������
		i_blk_size += (uint32_t)((i_blk_size % filesize) / (filesize / i_blk_size));
	}

	return i_blk_size;
}


static uint32_t rsync_sum_sizes_sqroot(uint64_t len)
{
	uint32_t blength;
	int64_t l;

	if (len <= XDELTA_BLOCK_SIZE * XDELTA_BLOCK_SIZE)
		blength = XDELTA_BLOCK_SIZE;
	else {
		int32_t max_blength = MAX_XDELTA_BLOCK_BYTES;
		int32_t c;
		int cnt;
		for (c = 1, l = len, cnt = 0; l >>= 2; c <<= 1, cnt++) {}
		if (c < 0 || c >= max_blength)
			blength = max_blength;
		else {
		    blength = 0;
		    do {
			    blength |= c;
			    if (len < (int64_t)blength * blength)
				    blength &= ~c;
			    c >>= 1;
		    } while (c >= 8);	/* round to multiple of 8 */
		    blength = MAX(blength, XDELTA_BLOCK_SIZE);
		}
	}

	return blength;
}

uint32_t get_xdelta_block_size (const uint64_t filesize)
{
	return rsync_sum_sizes_sqroot (filesize);
}



void DLL_EXPORT get_file_digest (file_reader & reader
								, uchar_t digest[DIGEST_BYTES])
{
	const int buf_len = XDELTA_BUFFER_LEN; // four times of block.
	char_buffer <uchar_t> buf (XDELTA_BUFFER_LEN);
	rs_mdfour_t ctx;
	rs_mdfour_begin(&ctx);

	while (true) {
		int size = reader.read_file (buf.begin (), buf_len);
		if (size <= 0)
			break;
		rs_mdfour_update(&ctx, buf.begin (), size);
	}

	rs_mdfour_result (&ctx, digest);
}

} //namespace xdelta

