#include <unistd.h>
#include <stdio.h>
int main()
{
    char data[128];
 
    read(0, data, 128);
       for(int i=0;i<128;i++){
           if(data[i]=='\n' || data[i]=='\r'){
	      printf("yeah!\n");
	   }
       }
       
    return 0;
}
