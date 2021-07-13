#include "md5.h"    //md5
#include <stdio.h>
#include <string.h>

int main()
{
	//to md5
	unsigned char pwd[16] = "123456789";
    MD5_CTX md5;
    MD5Init(&md5);
    unsigned char decrypt[16];
    MD5Update(&md5, (unsigned char *)pwd, strlen(pwd) );
    MD5Final(&md5, decrypt);

    char token[128] = {0};
    char str[100] = { 0 };
    for (int i = 0; i < 16; i++)
    {
        sprintf(str, "%02x", decrypt[i]);
	strcat(token, str);
    }

    printf("%s",token);
    return 0;
}
