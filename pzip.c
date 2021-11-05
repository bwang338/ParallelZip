#include <assert.h>
#include<errno.h>
#include<stddef.h>
#include<stdint.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<threads.h>

char* zipLine(char *line){
	for (int i = 0; i < strlen(line); i++){
		char letter = line[i];
		int j = i+1;
		int count = 1;
		while (letter == line[j]){
			count++;
			j++;
		}
		char number[100];
		sprintf(number, "%d", count);
		char *answer;
	}
}

int main(int argc, char *argv[]){
	char *answer = zipLine("aaabbbc");
}
