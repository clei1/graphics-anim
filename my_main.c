/*========== my_main.c ==========

  This is the only file you need to modify in order
  to get a working mdl project (for now).

  my_main.c will serve as the interpreter for mdl.
  When an mdl script goes through a lexer and parser,
  the resulting operations will be in the array op[].

  Your job is to go through each entry in op and perform
  the required action from the list below:

  push: push a new origin matrix onto the origin stack
  pop: remove the top matrix on the origin stack

  move/scale/rotate: create a transformation matrix
  based on the provided values, then
  multiply the current top of the
  origins stack by it.

  box/sphere/torus: create a solid object based on the
  provided values. Store that in a
  temporary matrix, multiply it by the
  current top of the origins stack, then
  call draw_polygons.

  line: create a line based on the provided values. Stores
  that in a temporary matrix, multiply it by the
  current top of the origins stack, then call draw_lines.

  save: call save_extension with the provided filename

  display: view the image live
  =========================*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "parser.h"
#include "symtab.h"
#include "y.tab.h"

#include "matrix.h"
#include "ml6.h"
#include "display.h"
#include "draw.h"
#include "stack.h"
#include "gmath.h"


/*======== void first_pass() ==========
  Inputs:
  Returns:

  Checks the op array for any animation commands
  (frames, basename, vary)

  Should set num_frames and basename if the frames
  or basename commands are present

  If vary is found, but frames is not, the entire
  program should exit.

  If frames is found, but basename is not, set name
  to some default value, and print out a message
  with the name being used.
  ====================*/
void first_pass() {
  //in order to use name and num_frames throughout
  //they must be extern variables
  extern int num_frames;
  extern char name[128];

  int vary_found = 0;
  int frames_found = 0;
  int name_found = 0;
  
  for(int i = 0; i<lastop; i++){
    switch (op[i].opcode)
      {
      case FRAMES:
	num_frames = op[i].op.frames.num_frames;
	frames_found = 1;
	break;
      case BASENAME:
	strncpy(name, op[i].op.basename.p->name, sizeof(name));
	name_found = 1;
	break;
      case VARY:
	vary_found = 1;
	break;
      }
  }//endfor

  if(vary_found == 1 && frames_found == 0){
    exit(1);
  }

  if(frames_found == 1 && name_found == 0){
    strncpy(name, "basename", sizeof(name));
    printf("Basename used: basename\n");
  }
}

/*======== struct vary_node ** second_pass() ==========
  Inputs:
  Returns: An array of vary_node linked lists

  In order to set the knobs for animation, we need to keep
  a seaprate value for each knob for each frame. We can do
  this by using an array of linked lists. Each array index
  will correspond to a frame (eg. knobs[0] would be the first
  frame, knobs[2] would be the 3rd frame and so on).

  Each index should contain a linked list of vary_nodes, each
  node contains a knob name, a value, and a pointer to the
  next node.

  Go through the opcode array, and when you find vary, go
  from knobs[0] to knobs[frames-1] and add (or modify) the
  vary_node corresponding to the given knob with the
  appropirate value.
  ====================*/
struct vary_node ** second_pass() {
  struct vary_node **knobs = calloc(num_frames, sizeof(struct vary_node));

  double start_frame, end_frame, start_val, end_val, d;
  struct vary_node * knob, prev;
  
  for (int i=0; i<lastop; i++){
    if(op[i].opcode == VARY){

      start_frame = op[i].op.vary.start_frame;
      end_frame = op[i].op.vary.end_frame;
      start_val = op[i].op.vary.start_val;
      end_val = op[i].op.vary.end_val;
      d = (start_val - end_val)/(start_frame - end_frame);
      
      for(int j=start_frame; j<=end_frame; j++){
        knob = (struct vary_node *) malloc(sizeof(struct vary_node));
	strncpy(knob->name, op[i].op.vary.p->name, sizeof(knob->name));
	knob->value = d * (start_frame - j) + start_val;
	knob->next = knobs[j];
	knobs[j] = knob;
      }
      
    }//end if
  }

  return knobs;
}

/*======== void print_knobs() ==========
  Inputs:
  Returns:

  Goes through symtab and display all the knobs and their
  current values
  ====================*/
void print_knobs() {
  int i;

  printf( "ID\tNAME\t\tTYPE\t\tVALUE\n" );
  for ( i=0; i < lastsym; i++ ) {

    if ( symtab[i].type == SYM_VALUE ) {
      printf( "%d\t%s\t\t", i, symtab[i].name );

      printf( "SYM_VALUE\t");
      printf( "%6.2f\n", symtab[i].s.value);
    }
  }
}

/*======== void my_main() ==========
  Inputs:
  Returns:

  This is the main engine of the interpreter, it should
  handle most of the commands in mdl.

  If frames is not present in the source (and therefore
  num_frames is 1), then process_knobs should be called.

  If frames is present, the enitre op array must be
  applied frames time. At the end of each frame iteration
  save the current screen to a file named the
  provided basename plus a numeric string such that the
  files will be listed in order, then clear the screen and
  reset any other data structures that need it.

  Important note: you cannot just name your files in
  regular sequence, like pic0, pic1, pic2, pic3... if that
  is done, then pic1, pic10, pic11... will come before pic2
  and so on. In order to keep things clear, add leading 0s
  to the numeric portion of the name. If you use sprintf,
  you can use "%0xd" for this purpose. It will add at most
  x 0s in front of a number, if needed, so if used correctly,
  and x = 4, you would get numbers like 0001, 0002, 0011,
  0487
  ====================*/
void my_main() {

  int i, f;
  struct matrix *tmp;
  struct stack *systems;
  screen t;
  zbuffer zb;
  color g;
  g.red = 0;
  g.green = 0;
  g.blue = 0;
  double step_3d = 20;
  double theta;
  double knob_value, xval, yval, zval;

  //Lighting values here for easy access
  color ambient;
  double light[2][3];
  double view[3];
  double areflect[3];
  double dreflect[3];
  double sreflect[3];

  ambient.red = 50;
  ambient.green = 50;
  ambient.blue = 50;

  light[LOCATION][0] = 0.5;
  light[LOCATION][1] = 0.75;
  light[LOCATION][2] = 1;

  light[COLOR][RED] = 0;
  light[COLOR][GREEN] = 255;
  light[COLOR][BLUE] = 255;

  view[0] = 0;
  view[1] = 0;
  view[2] = 1;

  areflect[RED] = 0.1;
  areflect[GREEN] = 0.1;
  areflect[BLUE] = 0.1;

  dreflect[RED] = 0.5;
  dreflect[GREEN] = 0.5;
  dreflect[BLUE] = 0.5;

  sreflect[RED] = 0.5;
  sreflect[GREEN] = 0.5;
  sreflect[BLUE] = 0.5;

  systems = new_stack();
  tmp = new_matrix(4, 1000);
  clear_screen( t );
  clear_zbuffer(zb);

  first_pass();
  struct vary_node **knobs = second_pass();
  print_knobs();

  for(f = 0; f < num_frames; f++){
    
    struct vary_node * knob = knobs[f];

    while(knob){ //go through linked list
      set_value(lookup_symbol(knob->name), knob->value);
      knob = knob->next;
    }
    
    for (i = 0; i < lastop; i++) {
      //printf("%d: ",i);
      switch (op[i].opcode)
	{
	case SPHERE:
	  add_sphere(tmp, op[i].op.sphere.d[0],
		     op[i].op.sphere.d[1],
		     op[i].op.sphere.d[2],
		     op[i].op.sphere.r, step_3d);
	  matrix_mult( peek(systems), tmp );
	  draw_polygons(tmp, t, zb, view, light, ambient,
			areflect, dreflect, sreflect);
	  tmp->lastcol = 0;
	  break;
	case TORUS:
	  add_torus(tmp,
		    op[i].op.torus.d[0],
		    op[i].op.torus.d[1],
		    op[i].op.torus.d[2],
		    op[i].op.torus.r0,op[i].op.torus.r1, step_3d);
	  matrix_mult( peek(systems), tmp );
	  draw_polygons(tmp, t, zb, view, light, ambient,
			areflect, dreflect, sreflect);
	  tmp->lastcol = 0;
	  break;
	case BOX:
	  add_box(tmp,
		  op[i].op.box.d0[0],op[i].op.box.d0[1],
		  op[i].op.box.d0[2],
		  op[i].op.box.d1[0],op[i].op.box.d1[1],
		  op[i].op.box.d1[2]);
	  matrix_mult( peek(systems), tmp );
	  draw_polygons(tmp, t, zb, view, light, ambient,
			areflect, dreflect, sreflect);
	  tmp->lastcol = 0;
	  break;
	case LINE:
	  add_edge(tmp,
		   op[i].op.line.p0[0],op[i].op.line.p0[1],
		   op[i].op.line.p0[2],
		   op[i].op.line.p1[0],op[i].op.line.p1[1],
		   op[i].op.line.p1[2]);
	  matrix_mult( peek(systems), tmp );
	  draw_lines(tmp, t, zb, g);
	  tmp->lastcol = 0;
	  break;
	case MOVE:
	  xval = op[i].op.move.d[0];
	  yval = op[i].op.move.d[1];
	  zval = op[i].op.move.d[2];
	  printf("Move: %6.2f %6.2f %6.2f",
		 xval, yval, zval);
	  if (op[i].op.move.p != NULL)
	    {
	      printf("\tknob: %s",op[i].op.move.p->name);
	    }
	  tmp = make_translate( xval, yval, zval );
	  matrix_mult(peek(systems), tmp);
	  copy_matrix(tmp, peek(systems));
	  tmp->lastcol = 0;
	  break;
	case SCALE:
	  xval = op[i].op.scale.d[0];
	  yval = op[i].op.scale.d[1];
	  zval = op[i].op.scale.d[2];
	  printf("Scale: %6.2f %6.2f %6.2f",
		 xval, yval, zval);
	  if (op[i].op.scale.p != NULL)
	    {
	      printf("\tknob: %s",op[i].op.scale.p->name);
	    }
	  tmp = make_scale( xval, yval, zval );
	  matrix_mult(peek(systems), tmp);
	  copy_matrix(tmp, peek(systems));
	  tmp->lastcol = 0;
	  break;
	case ROTATE:
	  xval = op[i].op.rotate.axis;
	  theta = op[i].op.rotate.degrees;
	  printf("Rotate: axis: %6.2f degrees: %6.2f",
		 xval, theta);
	  if (op[i].op.rotate.p != NULL)
	    {
	      printf("\tknob: %s",op[i].op.rotate.p->name);
	    }
	  theta*= (M_PI / 180);
	  if (op[i].op.rotate.axis == 0 )
	    tmp = make_rotX( theta );
	  else if (op[i].op.rotate.axis == 1 )
	    tmp = make_rotY( theta );
	  else
	    tmp = make_rotZ( theta );

	  matrix_mult(peek(systems), tmp);
	  copy_matrix(tmp, peek(systems));
	  tmp->lastcol = 0;
	  break;
	case PUSH:
	  //printf("Push");
	  push(systems);
	  break;
	case POP:
	  //printf("Pop");
	  pop(systems);
	  break;
	case SAVE:
	  //printf("Save: %s",op[i].op.save.p->name);
	  save_extension(t, op[i].op.save.p->name);
	  break;
	case DISPLAY:
	  //printf("Display");
	  display(t);
	  break;
	} //end opcode switch
      printf("\n");
    }//end operation loop

    if(num_frames > 1){
      char pic_name[128];
      sprintf(pic_name, "anim/%s%03d.png", name, f);
      save_extension(t, pic_name);
      systems = new_stack();
      tmp = new_matrix(4, 1000);
      clear_screen( t );
      clear_zbuffer(zb);
    }
    
  }//end frame loop

  if(num_frames > 1)
    make_animation(name);
  free(knobs);
  
}//end my_main
