/* 
 * File:   red.h
 * Author: ebuu
 *
 * Created on 31 juillet 2014, 14:43
 */

#ifndef RED_H
#define	RED_H

#include "config.h"
#include <vector>

class RTPRedundantPayload
{
public:
	RTPRedundantPayload(BYTE *data,DWORD size);

	void ParseRed( BYTE *data,DWORD size );

	BYTE* GetPrimaryPayloadData() 		const { return primaryData;	}
	DWORD GetPrimaryPayloadSize()		const { return primarySize;	}
	BYTE  GetPrimaryType()			const { return primaryType;	}
	BYTE  GetPrimaryCodec()			const { return primaryCodec;	}
	void  SetPrimaryCodec(BYTE codec)	      { primaryCodec = codec;	}

	//RTPTimedPacket* CreatePrimaryPacket();

	BYTE  GetRedundantCount()		const { return headers.size();	}
	BYTE* GetRedundantPayloadData(int i)	const { return i<headers.size()?redundantData+headers[i].ini:NULL;	}
	DWORD GetRedundantPayloadSize(int i) 	const { return i<headers.size()?headers[i].size:0;			}
	BYTE  GetRedundantType(int i)		const { return i<headers.size()?headers[i].type:0;			}
	BYTE  GetRedundantCodec(int i)		const { return i<headers.size()?headers[i].codec:0;			}
	BYTE  GetRedundantOffser(int i)		const { return i<headers.size()?headers[i].offset:0;			}
	int  GetRedundantTimestampOffset(int i)	const { return i<headers.size()? headers[i].offset:0;	}
	void  SetRedundantCodec(int i,BYTE codec)     { if (i<headers.size()) headers[i].codec = codec;			}

public:
	struct RedHeader
	{
		BYTE  type;
		BYTE  codec;
		DWORD offset;
		DWORD ini;
		DWORD size;
		RedHeader(BYTE type,DWORD offset,DWORD ini,DWORD size)
		{
			this->codec = type;
			this->type = type;
			this->offset = offset;
			this->ini = ini;
			this->size = size;
		};
		
	};
protected:
	std::vector<RedHeader>  headers;
	BYTE	primaryType;
	BYTE	primaryCodec;
	DWORD	primarySize;
	BYTE*	primaryData;
	BYTE*	redundantData;
};

#endif	/* RED_H */

