#include <stdio.h>
void conv(int(*out)[13][13], int(*w) [256][3][3], int(*in) [16][16]) {
     int row, col;
     int to, ti;
     int i, j;
     for(row = 0; row<13; row++){
	for(col = 0; col<13; col++){
	   for(to = 0; to<192; to++){
	      for(ti = 0; ti<256; ti++){
		 for(i = 0; i<3; i++){
		    for(j = 0; j<3; j++){
			out[to][row][col] += w[to][ti][i][j]*in[ti][row+i][col+j];
		    }
		 }
	      }
    	   } 
	}
     }
 
 }

