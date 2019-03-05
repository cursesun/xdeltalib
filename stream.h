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
// author:yeyouqun@163.com
/// @file
/// �����������ݴ�����ࡣ
namespace xdelta {

class xdelta_observer;

/// \class
/// Tcp ���� hasher ���ݵ����͡�
class tcp_hasher_stream : public hasher_stream
{
	virtual void add_block (const uint32_t fhash, const slow_hash & shash);
	virtual void end_hash_stream (const uchar_t file_hash[DIGEST_BYTES], const uint64_t filsize);
	virtual void set_holes (std::set<hole_t> * holeset) { assert (false); }
	virtual bool end_first_round (const uchar_t file_hash[DIGEST_BYTES]);
	virtual void next_round (const int32_t blk_len);
	virtual void end_one_round ();
	void _reconstruct_it ();
protected:
	virtual void on_error (const std::string & errmsg, const int errorno);
	virtual void start_hash_stream (const std::string & fname, const int32_t blk_len);
	void _streamize (char_buffer<uchar_t> & buff, bool now = false);
	void _end_one_stage (uint16_t endtype, const uchar_t file_hash[DIGEST_BYTES]);
	void _receive_construct_data (reconstructor & reconst);

	CActiveSocket		&	client_;			///< ������������
	char_buffer<uchar_t>	header_buff_;		///< ��ͷ���塣
	char_buffer<uchar_t>	stream_buff_;		///< �������棬���е������Ȼ����ڱ������У������ͻ��߽������͡�
	xdelta_observer &		observer_;			///< ͬ���۲�����
	file_operator &			fop_;				///< �ļ���������
	std::string				filename_;			///< ��ǰ������ļ�����
	int32_t					blk_len_;			///< ��ǰ�鳤�ȡ�
	int32_t					error_no_;			///< ִ�й����еĴ����롣
	bool					multiround_;		///< ���� Hash ͬ����
	bool					inplace_;			///< �͵ع����ļ��������� multiround_ ͬʱΪ�档
public:
	tcp_hasher_stream (CActiveSocket & client
					, file_operator & fop
					, xdelta_observer & observer
					, bool inplace = false);
	~tcp_hasher_stream () {}
};

///// \class
///// Tcp ���Ͷ��� hasher ���ݵ����͡�
//class multiround_tcp_stream : public tcp_hasher_stream
//{
//	virtual void start_hash_stream (const std::string & fname, const int32_t blk_len);
//	virtual void end_hash_stream (const uchar_t file_hash[DIGEST_BYTES], const uint64_t filsize);
//	virtual void set_holes (std::set<hole_t> * holeset);
//	virtual bool end_first_round (const uchar_t file_hash[DIGEST_BYTES]);
//	virtual void next_round (const int32_t blk_len);
//	virtual void end_one_round ();
//private:
//	bool _end_first_round;
//	bool _receive_equal_node ();
//	multiround_reconstructor reconst_;
//public:
//	multiround_tcp_stream (CActiveSocket & client
//					, file_operator & fop
//					, xdelta_observer & observer);
//	~multiround_tcp_stream () {}
//};
//
///// \var MULTI_ROUND_FLAG
///// ���ֱ�־�ֽڳ���
//extern const uint16_t MULTI_ROUND_FLAG;
///// \var SINGLE_ROUND_FLAG
///// ���ֱ�־�ֽڳ���
//extern const uint16_t SINGLE_ROUND_FLAG;
///// \var INPLACE_FLAG
///// �͵ع����־�ֽڳ���
//extern const uint16_t INPLACE_FLAG;

} // namespace xdelta

