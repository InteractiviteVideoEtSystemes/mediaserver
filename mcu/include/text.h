#ifndef _TEXT_H_
#define _TEXT_H_
#include "config.h"
#include "tools.h"
#include "amf.h"
#include "partedmosaic.h"
#include "media.h"
#include "log.h"

class TextFrame : public MediaFrame
{
public:
	TextFrame() : MediaFrame(MediaFrame::Text,0)
	{
	}
	
	TextFrame(DWORD ts,const BYTE *buffer,DWORD bufferLen) : MediaFrame(MediaFrame::Text,bufferLen)
	{
		SetFrame(ts,buffer,bufferLen);
	}
	
	TextFrame(DWORD ts,const std::wstring& str) : MediaFrame(MediaFrame::Text,str.length()*4)
	{
		SetFrame(ts,str);
	}

	TextFrame(DWORD ts,const wchar_t* data,DWORD size) : MediaFrame(MediaFrame::Text,size*4)
	{
		SetFrame(ts,data,size);
	}

	void SetFrame(DWORD ts,const BYTE *newBuffer,DWORD bufferLen)
	{
		//Store value
		SetTimestamp(ts);
		//Reset parser
		parser.Reset();
		//Set lenght to be parsed
		parser.SetSize(bufferLen);
		//Parse
		parser.Parse(newBuffer,bufferLen);

		//Serialize to buffer
		DWORD len = parser.Serialize(GetData(),GetMaxMediaLength());
		//Set it
		SetLength(len);
	}

	void SetFrame(DWORD ts,const wchar_t *data,DWORD size)
	{
		//Store value
		SetTimestamp(ts);
		//Parse
		parser.SetWChar(data,size);
		//Serialize to buffer
		DWORD len = parser.Serialize(GetData(),GetMaxMediaLength());
		//Set it
		SetLength(len);
	}

	void SetFrame(DWORD ts,const std::wstring& str)
	{
		//Store value
		SetTimestamp(ts);
		//Parse
		parser.SetWString(str);
		//Serialize to buffer
		DWORD len = parser.Serialize(GetData(),GetMaxMediaLength());
		//Set it
		SetLength(len);
	}

	virtual MediaFrame* Clone()
	{
		//Create new one
                TextFrame *frame = new TextFrame(GetTimeStamp(),GetData(),GetLength());
		//Return it
		return (MediaFrame*)frame;
	}

	std::wstring GetWString()	{ return parser.GetWString();	}
	const wchar_t* GetWChar()	{ return parser.GetWChar();	}
	DWORD GetWLength()		{ return parser.GetLength();	}
private:
	UTF8Parser parser;
};

class TextInput
{
public:
	virtual TextFrame* GetFrame(DWORD timeout)=0;
	virtual void Cancel()=0;
};

class TextOutput
{
public:
	virtual int SendFrame(TextFrame &frame)=0;
};

#endif
