`timescale 1ns/1ps
module multibox_video #(
 parameter integer H_ACTIVE=1280, V_ACTIVE=720,
 parameter integer ROI_W=1248, ROI_H=624, X_OFFSET=16, Y_OFFSET=48
)(
 input wire clk,resetn,
 input wire [511:0] active_coords, input wire [127:0] active_classes,
 input wire [3:0] active_mask,
 input wire hsync_i,vsync_i,de_i, input wire [23:0] rgb_i,
 output reg hsync_o,vsync_o,de_o, output reg [23:0] rgb_o,
 output wire frame_start
);
 reg vsync_d;
 reg [15:0] x,y;
 reg box_hit,label_bg_hit,label_text_hit;
 reg [31:0] x1,y1,x2,y2;
 reg [31:0] label_x,label_y,label_w,local_x,local_y;
 reg [1:0] class_id;
 reg [3:0] char_index;
 reg [2:0] char_col,char_row;
 reg [7:0] character;
 integer k;
 assign frame_start=vsync_i && !vsync_d;
 function automatic [31:0] clip(input [31:0] v,input integer limit);
  clip=(v>=limit)?limit-1:v;
 endfunction
 function automatic [3:0] label_length(input [1:0] cls);
  case(cls)
   2'd0:label_length=7; // scratch
   2'd1:label_length=4; // dent
   2'd2:label_length=5; // stain
   default:label_length=0;
  endcase
 endfunction
 function automatic [7:0] label_character(input [1:0] cls,input [3:0] index);
  begin
   label_character=8'h20;
   case(cls)
    2'd0:case(index)
     0:label_character="s";1:label_character="c";2:label_character="r";
     3:label_character="a";4:label_character="t";5:label_character="c";
     6:label_character="h";default:label_character=8'h20;
    endcase
    2'd1:case(index)
     0:label_character="d";1:label_character="e";2:label_character="n";
     3:label_character="t";default:label_character=8'h20;
    endcase
    2'd2:case(index)
     0:label_character="s";1:label_character="t";2:label_character="a";
     3:label_character="i";4:label_character="n";default:label_character=8'h20;
    endcase
    default:label_character=8'h20;
   endcase
  end
 endfunction
 function automatic glyph_pixel(input [7:0] ch,input [2:0] col,input [2:0] row);
  reg [4:0] bits;
  begin
   bits=5'b00000;
   case(ch)
    "a":case(row) 0:bits=5'b01110;1:bits=5'b10001;2:bits=5'b10001;3:bits=5'b11111;4:bits=5'b10001;5:bits=5'b10001;6:bits=5'b10001;endcase
    "c":case(row) 0:bits=5'b01111;1:bits=5'b10000;2:bits=5'b10000;3:bits=5'b10000;4:bits=5'b10000;5:bits=5'b10000;6:bits=5'b01111;endcase
    "d":case(row) 0:bits=5'b11110;1:bits=5'b10001;2:bits=5'b10001;3:bits=5'b10001;4:bits=5'b10001;5:bits=5'b10001;6:bits=5'b11110;endcase
    "e":case(row) 0:bits=5'b11111;1:bits=5'b10000;2:bits=5'b10000;3:bits=5'b11110;4:bits=5'b10000;5:bits=5'b10000;6:bits=5'b11111;endcase
    "h":case(row) 0:bits=5'b10001;1:bits=5'b10001;2:bits=5'b10001;3:bits=5'b11111;4:bits=5'b10001;5:bits=5'b10001;6:bits=5'b10001;endcase
    "i":case(row) 0:bits=5'b11111;1:bits=5'b00100;2:bits=5'b00100;3:bits=5'b00100;4:bits=5'b00100;5:bits=5'b00100;6:bits=5'b11111;endcase
    "n":case(row) 0:bits=5'b10001;1:bits=5'b11001;2:bits=5'b11001;3:bits=5'b10101;4:bits=5'b10011;5:bits=5'b10011;6:bits=5'b10001;endcase
    "r":case(row) 0:bits=5'b11110;1:bits=5'b10001;2:bits=5'b10001;3:bits=5'b11110;4:bits=5'b10100;5:bits=5'b10010;6:bits=5'b10001;endcase
    "s":case(row) 0:bits=5'b01111;1:bits=5'b10000;2:bits=5'b10000;3:bits=5'b01110;4:bits=5'b00001;5:bits=5'b00001;6:bits=5'b11110;endcase
    "t":case(row) 0:bits=5'b11111;1:bits=5'b00100;2:bits=5'b00100;3:bits=5'b00100;4:bits=5'b00100;5:bits=5'b00100;6:bits=5'b00100;endcase
    default:bits=5'b00000;
   endcase
   glyph_pixel=(col<5 && row<7)?bits[4-col]:1'b0;
  end
 endfunction
 always @* begin
  box_hit=0;label_bg_hit=0;label_text_hit=0;
  x1=0;y1=0;x2=0;y2=0;label_x=0;label_y=0;label_w=0;
  local_x=0;local_y=0;class_id=0;char_index=0;char_col=0;char_row=0;character=8'h20;
  for(k=0;k<4;k=k+1) begin
   x1=clip(active_coords[(k*4)*32+:32],ROI_W)+X_OFFSET;
   y1=clip(active_coords[(k*4+1)*32+:32],ROI_H)+Y_OFFSET;
   x2=clip(active_coords[(k*4+2)*32+:32],ROI_W)+X_OFFSET;
   y2=clip(active_coords[(k*4+3)*32+:32],ROI_H)+Y_OFFSET;
   if(active_mask[k] && x1<x2 && y1<y2 && de_i && x<H_ACTIVE && y<V_ACTIVE)
    if(((y==y1 || y==y2) && x>=x1 && x<=x2) ||
       ((x==x1 || x==x2) && y>=y1 && y<=y2)) box_hit=1;
   class_id=active_classes[k*32+:2];
   label_x=x1;label_y=(y1>=18)?y1-18:y1+1;
   label_w=label_length(class_id)*12+4;
   if(active_mask[k] && label_w>4 && de_i && x<H_ACTIVE && y<V_ACTIVE &&
      x>=label_x && x<label_x+label_w && y>=label_y && y<label_y+18) begin
    label_bg_hit=1;
    if(x>=label_x+2 && y>=label_y+2) begin
     local_x=x-label_x-2;local_y=y-label_y-2;
     char_index=local_x/12;char_col=(local_x%12)>>1;char_row=local_y>>1;
     character=label_character(class_id,char_index);
     if(char_index<label_length(class_id) && local_y<14 && char_col<5 &&
        glyph_pixel(character,char_col,char_row)) label_text_hit=1;
    end
   end
  end
 end
 always @(posedge clk) begin
  if(!resetn) begin
   vsync_d<=0;x<=0;y<=0;hsync_o<=0;vsync_o<=0;de_o<=0;rgb_o<=0;
  end else begin
   vsync_d<=vsync_i;
   // RGB, sync and DE are all delayed by exactly one clock.
   hsync_o<=hsync_i;vsync_o<=vsync_i;de_o<=de_i;
   rgb_o<=label_text_hit?24'hffffff:((box_hit||label_bg_hit)?24'hff0000:rgb_i);
   if(hsync_i || vsync_i) x<=0;
   else if(de_i) x<=x+1;
   if(vsync_i) y<=0;
   else if(de_i && x==H_ACTIVE-1) y<=y+1;
  end
 end
endmodule
