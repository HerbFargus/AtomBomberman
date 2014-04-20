#include "iniparser.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <string>
#include <ctype.h>
#include <sstream>



// constants
const char kGroupBegin   = '[';
const char kGroupEnd     = ']';
const char kCommentBegin = ';';
const char kKeyValDelim  = '=';


FileIO::FileIO(const char* path)
{
	m_path = path;
	m_file = NULL;
	m_read = 0;
	m_pos  = 0;
	m_done = 0;
	m_file_buf = NULL;
}

FileIO::~FileIO()
{
}

int FileIO::init_reading()
{
	m_file = fopen(m_path, "r");
	if (!m_file) {
		m_error = "Unable to open " + id() + ": " + strerror(errno) + "\n";
		return -1;
	}
	return 0;
}

void FileIO::finish_reading()
{
	if (m_file) {
		fclose(m_file);
		m_file = NULL;
	}
}

int FileIO::init_writing()
{
	m_file = fopen(m_path, "r");
	if (!m_file) {
		m_error = "Unable to open " + id() + ": " + strerror(errno) + "\n";
		return -1;
	}
	
	fseek(m_file, 0, SEEK_END);
	m_file_len = ftell(m_file);
	fseek(m_file, 0, SEEK_SET);
	
	m_file_buf = (char*)malloc(m_file_len);
	if (!m_file_buf) {
		m_error = "Unable to malloc memory for file " + id() + "\n";
		return -1;
	}
	
	fread(m_file_buf, m_file_len, 1, m_file);
	fclose(m_file);
	
	m_file = fopen(m_path, "w");
	if (!m_file) {
		m_error = "Unable to open " + id() + ": " + strerror(errno) + "\n";
		return -1;
	}
	
	m_pos = 0;
	
	return 0;
}

void FileIO::finish_writing()
{
	if (m_file) {
		fclose(m_file);
		m_file = NULL;
	}
	
	if (m_file_buf) {
		free(m_file_buf);
		m_file_buf = NULL;
	}
}

int FileIO::write(int pos)
{
	int to_write = std::min(pos, m_file_len) - m_pos;
	
	fwrite(&m_file_buf[m_pos], to_write, 1, m_file);
	m_pos += to_write;
	
	if (m_pos == m_file_len) {
		return -1;
	}
	return 0;
}

int FileIO::skip(int pos)
{
	m_pos = std::min(pos, m_file_len);
	
	if (m_pos == m_file_len) {
		return -1;
	}
	return 0;
}

std::string FileIO::id()
{
	return std::string(m_path);
}

std::string FileIO::get_error()
{
	return m_error;
}

int FileIO::getc()
{
	if (m_pos >= m_read) {
		m_read = fread(m_buf, 1, sizeof(m_buf), m_file);
		m_done += m_pos;
		m_pos = 0;
		if (m_read == 0)
			return -1;
	}
	return m_buf[m_pos++];
}

int FileIO::ungetc(int c)
{
	if (m_pos > 0)
		m_pos--;
	return c;
}

int FileIO::pos()
{
	return m_done + m_pos;
}

void FileIO::skip_space()
{
	int c;
	while (c = getc(), c == ' ' || c == '\t')
		continue;
	ungetc(c);
}

void FileIO::skip_line()
{
	int c;
	while (c = getc(), c != EOF && c != '\n')
		continue;
}

static int isname(char c)
{
	return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
	                || (c >= '0' && c <= '9');
}

static int isval(char c)
{
	return c != kCommentBegin && c != EOF && c != ' ' && c != '\t' && c != '\n' && c != '\r';
}

// reads group/key [_A-Za-z0-9]*
std::string FileIO::read_name()
{
	int c, pos=0;
	char buf[2048];
	
	while (c = getc(), isname(c) && pos < (int)sizeof(buf))
		buf[pos++] = c;
	ungetc(c);
	
	return std::string(buf, buf+pos);
}

// reads val [^\s;]*
std::string FileIO::read_val()
{
	int c, pos=0;
	char buf[2048];
	
	while (c = getc(), isval(c) && pos < (int)sizeof(buf))
		buf[pos++] = c;
	ungetc(c);
	
	return std::string(buf, buf+pos);
}

IniParser::IniParser(FileIO* io)
{
	m_io = io;
}

IniParser::~IniParser()
{
	delete m_io;
}

ParsedLine& IniParser::parse_line()
{
	int c;
	static ParsedLine parsed_line;
	
	m_io->skip_space();
	c = m_io->getc();
	
	switch (c) {
	case '\n':
		parsed_line.type = EMPTY;
		return parsed_line;
	
	case EOF:
		parsed_line.type = END_OF_INI;
		return parsed_line;
	
	case kCommentBegin:
		parsed_line.type = COMMENT;
		m_io->skip_line();
		return parsed_line;
	
	case kGroupBegin:
		parsed_line.type = GROUP;
		m_io->skip_space();
		parsed_line.group.pos = m_io->pos();
		parsed_line.group.str = m_io->read_name();
		parsed_line.group.end = m_io->pos();
		m_io->skip_space();
		if (m_io->getc() != kGroupEnd) {
			parsed_line.error = std::string("expected '")+kGroupEnd+"'";
			parsed_line.type = ERROR;
			return parsed_line;
		}
		m_io->skip_line();
		return parsed_line;
	
	default:
		if (!isname(c))
			break;
		
		parsed_line.type = KEY_VAL;
		m_io->ungetc(c);
		
		// read key
		parsed_line.key.pos = m_io->pos();
		parsed_line.key.str = m_io->read_name();
		parsed_line.key.end = m_io->pos();
		m_io->skip_space();
		if (m_io->getc() != kKeyValDelim) {
			parsed_line.error = std::string("expected '")+kKeyValDelim+"'";
			parsed_line.type = ERROR;
			return parsed_line;
		}
		
		// read val
		m_io->skip_space();
		parsed_line.val.pos = m_io->pos();
		parsed_line.val.str = m_io->read_val();
		parsed_line.val.end = m_io->pos();
		m_io->skip_line();
		
		return parsed_line;
	}
	
	parsed_line.error = std::string("expected '")+kGroupBegin+"' or key";
	parsed_line.type = ERROR;
	return parsed_line;
}

int IniParser::parse_ini()
{
	int n_line = 0;
	int last_pos = 0;
	GroupInfo *group_info = NULL;
	EntryInfo *entry_info = NULL;
	
	if (m_io->init_reading() == -1) {
		m_error = m_io->get_error();
		return -1;
	}
	
	for (ParsedLine& l = parse_line(); l.type != END_OF_INI; parse_line()) {
		switch (l.type) {
		case ERROR: {
			std::stringstream ss;
			ss << "Error parsing " << m_io->id() << " at line " << n_line+1
			   << ", pos " << m_io->pos()-last_pos << " (" << l.error << ")\n";
			m_error = ss.str();
			return -1;
		}
		
		case EMPTY:
		case COMMENT:
		case END_OF_INI: break;
		
		case GROUP:
			// constructor activated on accessing key
			group_info = &m_groups[l.group.str];
			group_info->first_line_pos = m_io->pos();
			group_info->group = l.group;
			break;
		
		case KEY_VAL:
			// key/val pair without a group isn't allowed
			if (!group_info)
				break;
			entry_info = &group_info->entries[l.key.str];
			entry_info->key = l.key;
			entry_info->val = l.val;
			break;
		}
		
		last_pos = m_io->pos();
		n_line++;
	}
	
	m_io->finish_reading();
	
	return 0;
}

std::string IniParser::get_error()
{
	return m_error;
}

const char* IniParser::get_string(const char* group, const char* key, const char* def)
{
	if (!group || !key)
		return def;
	
	GroupInfoMap::iterator g = m_groups.find(group);
	if (g == m_groups.end())
		return def;
	
	EntryInfoMap::iterator e = g->second.entries.find(key);
	if (e == g->second.entries.end())
		return def;
	
	return e->second.val.str.c_str();
}

int IniParser::get_int(const char* group, const char* key, int def)
{
	const char *str = get_string(group, key);
	if (!str)
		return def;
	return strtol(str, 0, 0);
}

float IniParser::get_float(const char* group, const char* key, float def)
{
	const char *str = get_string(group, key);
	if (!str)
		return def;
	return strtof(str, 0);
}

int IniParser::set_string(const char* group, const char* key, const char* val)
{
	// val == NULL -> remove key/val ?
	// is key in group ?
	
	if (!group || !key) {
		m_error = "set_string called with " + std::string(!group ? "group" : "key") + "=NULL\n";
		return -1;
	}
	
	GroupInfoMap::iterator g = m_groups.find(group);
	if (g == m_groups.end()) {
		m_error = "Group [" + std::string(group) + "] doesn't exist\n";
		return -1;
	}
	
	EntryInfoMap::iterator e = g->second.entries.find(key);
	if (e == g->second.entries.end()) {
		// TODO: add key/val
	}
	
	//return e->second.val.str.c_str();
	
	e->second.val.str = val;
	m_dirty.insert(&e->second);
	
	return 0;
}

int IniParser::write_ini()
{
	if (m_io->init_writing() == -1) {
		m_error = m_io->get_error();
		return -1;
	}
	
	for (DirtySet::iterator it = m_dirty.begin(); it != m_dirty.end(); it++) {
		TokenInfo info = (*it)->val;
		printf("[%s] (%d-%d)\n", info.str.c_str(), info.pos, info.end);
	}
	return 0;
}

#ifdef USE_SDL_ZZIP
RWIO::RWIO(const char* path)
  : FileIO(path)
{
	m_data = NULL;
}

RWIO::~RWIO()
{
}

int RWIO::init_reading()
{
	m_data = SDL_RWFromZZIP(m_path, "r");
	if (!m_data) {
		m_error = "Unable to open " + id() + ": " + strerror(errno) + "\n";
		return -1;
	}
	return 0;
}

void RWIO::finish_reading()
{
	if (m_data) {
		SDL_RWclose(m_data);
		m_data = NULL;
	}
}

int RWIO::getc()
{
	char c;
	
	if (SDL_RWread(m_data, &c, 1, 1) == 0)
		return -1;
	
	return c;
}

int RWIO::ungetc(int c)
{
	if (c == EOF)
		return EOF;
	
	SDL_RWseek(m_data, -1, RW_SEEK_CUR);
	
	return c;
}

int RWIO::pos()
{
	return SDL_RWtell(m_data);
}
#endif

IniParser* ParseFILE(const char* path)
{
	IniParser *parser = new IniParser(new FileIO(path));
	
	if (parser->parse_ini() == -1) {
		fputs(parser->get_error().c_str(), stderr);
		delete parser;
		return NULL;
	}
	
	return parser;
}

#ifdef USE_SDL_ZZIP
IniParser* ParseZZIP(const char* path)
{
	IniParser *parser = new IniParser(new RWIO(path));
	
	if (parser->parse_ini() == -1) {
		fputs(parser->get_error().c_str(), stderr);
		delete parser;
		return NULL;
	}
	
	return parser;
}
#endif
