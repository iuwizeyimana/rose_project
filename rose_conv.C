#include <stdio.h>
#include "hls_stream.h" 
/*

void conv(int (*out)[13UL][13UL],int (*w)[256UL][3UL][3UL],int (*in)[16UL][16UL])
*/
void conv(hls::stream<int>& out, hls::stream<int>& w, hls::stream<int>& in)
{
  int row;
  int col;
  int to;
  int ti;
  int i;
  int j;
  int _lt_var_ti;
  for (_lt_var_ti = 0; _lt_var_ti <= 255; _lt_var_ti += 33) {
    int _lt_var_row;
    for (_lt_var_row = 0; _lt_var_row <= 12; _lt_var_row += 7) {
      int _lt_var_col;
      for (_lt_var_col = 0; _lt_var_col <= 12; _lt_var_col += 7) {
        int _lt_var_to;
        int out_buff[3][7][7];
        for (_lt_var_to = 0; _lt_var_to <= 191; _lt_var_to += 3) {
#pragma HLS DATAFLOW
          load_w:
          int w_buff[3][33][3][3];
          int hold_w;
          int d;
          int c;
          int b;
          int a;
          for (a = 0; a < 3; a++) {
            for (b = 0; b < 33; b++) {
              for (c = 0; c < 3; c++) {
                for (d = 0; d < 3; d++) {
if(!w.empty()){w>> hold_w; w_buff[a][b][c][d] = hold_w;}
                }
              }
            }
          }
          load_in:
          int in_buff[33][7][7];
          int hold_in;
          int c1;
          int b1;
          int a1;
          for (a1 = 0; a1 < 33; a1++) {
            for (b1 = 0; b1 < 7; b1++) {
              for (c1 = 0; c1 < 7; c1++) {
if(!in.empty()){in>> hold_in; in_buff[a1][b1][c1] = hold_in;}
              }
            }
          }
          compute:
          for (row = _lt_var_row; row <= (((12 < (_lt_var_row + 7 - 1))?12 : (_lt_var_row + 7 - 1))); row += 1) {
            for (col = _lt_var_col; col <= (((12 < (_lt_var_col + 7 - 1))?12 : (_lt_var_col + 7 - 1))); col += 1) {
              for (i = 0; i < 3; i++) {
                for (j = 0; j < 3; j++) {
                  for (ti = _lt_var_ti; ti <= (((255 < (_lt_var_ti + 33 - 1))?255 : (_lt_var_ti + 33 - 1))); ti += 1) {
#pragma HLS UNROLL factor = 33
                    for (to = _lt_var_to; to <= (((191 < (_lt_var_to + 3 - 1))?191 : (_lt_var_to + 3 - 1))); to += 1) {
                      
#pragma HLS UNROLL factor = 3
/* 
out[to][row][col] += (w[to][ti][i][j] * in[ti][row + i][col + j]);
*/ out_buff[to][row][col] += w_buff[to][ti][i][j] * in_buff[ti][row+i][col+j];
                    }
                  }
                }
              }
            }
          }
        }
        store_out:
        int c2;
        int b2;
        int a2;
        for (a2 = 0; a2 < 3; a2++) {
          for (b2 = 0; b2 < 7; b2++) {
            for (c2 = 0; c2 < 7; c2++) {
out<< out_buff[a2][b2][c2];
            }
          }
        }
      }
    }
  }
}
