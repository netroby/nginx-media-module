#ifndef __CMD5_H__
#define __CMD5_H__

/* MD5 Class. */
class CMD5 
{
public:
    CMD5();
    virtual ~CMD5();
    void MD5Update (const unsigned char *input, unsigned long inputLen);
    void MD5Final (unsigned char digest[16]);
    void MD5Print(char digest[16])const;
private:
    unsigned long int state[4];        /* state (ABCD) */
    unsigned long int count[2];        /* number of bits, modulo 2^64 (lsb first) */
    unsigned char buffer[64];       /* input buffer */
    unsigned char PADDING[64];        /* What? */

private:
    void MD5Init ();
    void MD5Reset();
    void MD5Transform (unsigned long int uiStates[4], const unsigned char block[64])const;
    void MD5_memcpy (unsigned char* output, const unsigned char* input,unsigned long len)const;
    bool MD5_memcompare (const unsigned char* src,const unsigned char * dst ,unsigned long len)const;
    void Encode (unsigned char *output, const unsigned long int *input,unsigned long len)const;
    void Decode (unsigned long int *output, const unsigned char *input, unsigned long len)const;
    void MD5_memset (unsigned char* output,long value,unsigned long len)const;
//private:
    //static char HexCharactor[16];
};

#endif
