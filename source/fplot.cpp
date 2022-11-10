/*****************************************************************************
 *
 *                       Calcium Calculator (CalC)
 *               Copyright (C) 2001-2019 Victor Matveev
 *
 *                               fplot.cpp
 *
 *  Defines all plot objects, including mute (file dump) plots
 *  and xmgr/xmgrace pipes
 *
 ****************************************************************************
 
    This file is part of Calcium Calculator (CalC).

    CalC is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CalC is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CalC.  If not, see <https://www.gnu.org/licenses/>

 ************************************************************************/

#include "stdafx.h"  // for compatibility with Visual C++
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <float.h>  // for compatibility with Visual C++
#include <string.h>
#include <stdarg.h>
#include "vector.h"
#include "syntax.h"
#include "box.h"
#include "grid.h"
#include "field.h"
#include "table.h"
#include "peak.h"
#include "markov.h"
#include "interpol.h"
#include "gate.h"
#include "fplot.h"
#include "simulation.h"

int    XmgrPlot::XMGR_STEPS         = 400;
int    PlotObj::UPDATE_STEPS        = 600;
int    PlotObj::UPDATE_STEPS_1D     = 200;
int    PlotObj::UPDATE_STEPS_BINARY = 40;
double PlotObj::UPDATE_ACCURACY     = 0.001; 

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//        C L A S S   P L O T O B J E C T   I M P L E M E N T A T I O N
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

void    StateDump::draw()
   {
   if ( Sim->Gates->Time < exportTime) { complete = false; return; }
   if ( complete ) return;
   complete = true;
   Sim->Export(fileName);
   };

//*************************************************************************************
//                M U T E    P L O T   D R A W
//*************************************************************************************

MutePointPlot::MutePointPlot(double *ptr, double *tptr, char islog,  double t, 
               const char *fname, const char *txt) : PlotObj(ptr, islog, txt)
    {
    tscale = double(UPDATE_STEPS) / t;
    fmin = fold = fmax = get_value();
    x_temp = x_value = -1e12;
    Time = tptr;
    f_temp = 0.0;
    strcpy(fileName,fname);
    file = fopenAssure(fname, "w", "plot", win_title);

	bufferSize = long(UPDATE_STEPS * 1.5);
	counter = 0;
	Tbuffer = new double[bufferSize];
	Ybuffer = new double[bufferSize];
    }

//*************************************************************************************

void   MutePointPlot::pushValue(double t, double y)  {

	fprintf(file, "%.10g %.10g \n", t, y);
	Tbuffer[counter] = t;
	Ybuffer[counter] = y;
	counter ++;

	if (counter == bufferSize)  {
		long bufferSizeNew = bufferSize + 100;
		double *newT = new double [bufferSizeNew];
		double *newY = new double [bufferSizeNew];

		if (VERBOSE > 5) fprintf(stderr, " >> Buffer size exceeded in MutePointPlot %s: increasing buffer size: %ld ==> %ld\n", fileName, bufferSize, bufferSizeNew);
		
		for (long i = 0; i < bufferSize; i++) 
		{
			newT[i] = Tbuffer[i];
			newY[i] = Ybuffer[i];
		}
		delete [] Tbuffer; delete [] Ybuffer;
		Tbuffer = newT;  Ybuffer = newY;
		bufferSize = bufferSizeNew;
	}
}

//*************************************************************************************

void    MutePointPlot::draw()    // file remains open for the duration of simulation
   {
   double   f = get_value();
   if      (f > fmax) fmax = f; 
   else if (f < fmin) fmin = f;

   if ( *Time < x_value && file != (FILE *)stdout  && file != (FILE *)stderr ) {  // do a back step

	 if (VERBOSE > 5) fprintf(stderr, " >> Instability recovery: back step in MutePointPlot %s, time: %g ==> %g\n", fileName, x_value, *Time);
	 rewind(file);
     long cnt, cntOld = counter;
	 counter = 0;

	 for (cnt = 0; cnt < cntOld; cnt++)
		 if (Tbuffer[cnt] < *Time)  pushValue(Tbuffer[cnt], Ybuffer[cnt]);
		 else break;
   }                // end of back-step
   else {
       bool redrawFlag = ( int( *Time * tscale ) == int( x_value * tscale) )    ? false : true;
       bool newFlag    = ( fabs(f - fold)   < (fmax - fmin) * UPDATE_ACCURACY ) ? false : true;
       bool tempFlag   = ( fabs(f - f_temp) < (fmax - fmin) * UPDATE_ACCURACY ) ? false : true;
       if ( !redrawFlag && !newFlag ) { x_temp = *Time; f_temp = f; return; }
       if  ( newFlag && tempFlag && (x_temp > x_value) )        // This allows drawing multiple intemediate
              pushValue(x_temp, f_temp);  // between time steps, if change is fast (?)
   }

   fold    = f_temp = f;
   x_value = x_temp = *Time; 
   pushValue(*Time, get_value() );
   fflush(file); 
   }



//**********************************************

void  FieldDumpT::draw()  {

	if (field->Time >= oldTime) {
		if (field->Time < newTime) return;
		else {
			oldTime = newTime;
			newTime = oldTime + timeBetweenSaves;
			dump();
		}
	} else {  // Do a back-step

		 if (VERBOSE > 5) fprintf(stderr, " >> Instability recovery: back step in FieldDump %s, time: %g ==> %g\n", fileName, oldTime, field->Time);
		 oldTime = field->Time;
		 
		 char tempFileName[512];     
	     sprintf(tempFileName, "plotTempBinFile%ld", long(rand()*1000000000) );

		 double tempTime;
		 double *buffer;
		 buffer = new double[field->size + 1];

		 FILE *temp = fopenAssure(tempFileName, "wb", "plot",  win_title);
		 file       = fopenAssure(fileName,     "rb", "plot",  win_title);

		 header(temp);
		 chuckHeader(file, buffer);

		 while (!feof(file)) {
		   fread ( (void *)&tempTime, sizeof(double), 1,           file);
		   fread ( (void *)buffer,    sizeof(double), field->size, file);
		   if (feof(file) || tempTime >= oldTime) break;
		   fwrite( (void *)&tempTime, sizeof(double), 1,           temp);
	       fwrite( (void *)buffer,    sizeof(double), field->size, temp);
		 }
		 fclose(temp); fclose(file);
		 
		 temp = fopenAssure(fileName,     "wb", "plot",  win_title);
		 file = fopenAssure(tempFileName, "rb", "plot",  win_title);
		 header(temp);
		 chuckHeader(file, buffer);

		 while (!feof(file)) {
			   fread ( (void *)&tempTime, sizeof(double), 1,           file);
			   fread ( (void *)buffer,    sizeof(double), field->size, file);
			   if (feof(file) ) break;
			   fwrite( (void *)&tempTime, sizeof(double), 1,           temp);
			   fwrite( (void *)buffer,    sizeof(double), field->size, temp);
		 }
		 fclose(temp); fclose(file); remove(tempFileName);
		 delete [] buffer; 

		 dump();
	} // end back-step
}
	 
	 
//*************************************************************************************

void  MutePlot1D::pushRow(double t, double *f, int cols)
{	
	if (counter >= bufferSize) {
		if (VERBOSE > 5) fprintf(stderr, "*** Warning: too many writes to MutePlot1D %s (time=%g, count %ld); overwriting last values\n", win_title, t, counter);
		counter--;
	}

	Tbuffer[counter] = t;
	if (file == 0)  file = fopenAssure(fileName, "w", "plot", win_title);


       for (int i = 0; i < num; i++) 
	   {
			Ybuffer[counter * num + i] = f[i];
			if (cols == 2)
				 fprintf(file,"%.6g  %.9g \n", coord[i], f[i]);
			else fprintf(file,"%.6g  %.6g  %.9g \n", t, coord[i], f[i]);
	   }

	fprintf(file,"\n");
	fflush(file);
	counter++;
}

//*************************************************************************************

void MutePlot1D::draw() {

   double t = field->Time;
   long ind = field_index;

   if (UPDATE_STEPS_1D == 1) {
     //fprintf(stderr,"t=%g export=%g t-e=%e fname=%s \n",t, exportTime, t - exportTime, fileName);
       if (t + 1e-20 < exportTime) { complete = false; return; }
       if ( complete ) return;
	   double *fValues = new double[num];
	   for (int i = 0; i < num; i++, ind += incr) fValues[i] = get_value(ind);
       pushRow(t, fValues, 2);
	   delete [] fValues;
	   complete = true;
   }
   else if ( t < x_value && !equal(fileName,"stdout")  && !equal(fileName,"stderr") ) 
   {  // do a back step
	 if (VERBOSE > 5) fprintf(stderr, " >> Instability recovery: back step in file plot %s, time: %g ==> %g\n", fileName, x_value, t);
	 long cnt, cntOld = counter;
	 counter = 0;
	 rewind(file);
	 for (cnt = 0; cnt < cntOld; cnt++)
		 if (Tbuffer[cnt] < t) pushRow(x_value = Tbuffer[cnt], Ybuffer + cnt * num, 3);
		 else break;
   }    // if no back-step is required:
   else if ( int(t * tscale) > int(x_value * tscale) )
       {
       x_value = t;
	   double *fValues = new double[num];
	   for (int i = 0; i < num; i++, ind += incr) fValues[i] = get_value(ind);
       pushRow(t, fValues, 3);
	   delete [] fValues;
       }

}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//         C L A S S   2 D - F I E L D   P L O T   I M P L E M E N T A T I O N
//*******************************************************************************

FieldPlot2D::FieldPlot2D(FieldObj *f, char islog, const char *dir, double coord, const char *label) : 
FieldPlotObj(f, islog, "", label)
{
 int ix=0, iy=0, iz=0;
 signed long si = -1;

  if ( equal(dir,LABEL_DIM1) && DIMENSIONALITY == 3) {                 // y-z plane
            grid1 = FieldObj::ygrid;  grid2  = FieldObj::zgrid;
            coord1= FieldObj::ycoord; coord2 = FieldObj::zcoord;
            incr1 = FieldObj::xsize;  incr2  = FieldObj::xysize; 
            num1  = FieldObj::ysize;  num2   = FieldObj::zsize;
			for (iy = 0; iy < field->ysize && si == -1; iy++)
				for (iz = 0; iz < field->zsize && si == -1; iz++)
					si = field->location_to_index(coord, coord1[iy], coord2[iz], 0);
			if (si == -1) field->location_to_index(coord, coord1[iy], coord2[iz], 1); // trigger error message
			else field_index = long(si);
			FieldObj::Grid->split(field_index, ix, iy, iz);
			field_index = ix;
			coord = FieldObj::xcoord[ix];
	    sprintf(win_title,"%s[%.3g,%s,%s]", f->ID, coord, LABEL_DIM2, LABEL_DIM3);
  } else 
    if ( equal(dir,LABEL_DIM2) && DIMENSIONALITY == 3 ) {             // x-z plane
            grid1  = FieldObj::xgrid;   grid2  = FieldObj::zgrid;
            coord1 = FieldObj::xcoord;  coord2 = FieldObj::zcoord;
            incr1  = 1;                 incr2  = FieldObj::xysize; 
            num1   = FieldObj::xsize;   num2   = FieldObj::zsize;
            for (ix = 0; ix < field->xsize && si == -1; ix++)
				for (iz = 0; iz < field->zsize && si == -1; iz++)
					si = field->location_to_index(coord1[ix], coord, coord2[iz], 0);
			if (si == -1) field->location_to_index(coord1[ix], coord, coord2[iz], 1); // trigger error message
			else field_index = long(si);
			FieldObj::Grid->split(field_index, ix, iy, iz);
			field_index = iy * FieldObj::xsize;
			coord = FieldObj::ycoord[iy];
            sprintf(win_title,"%s[%s,%.3g,%s]", f->ID, LABEL_DIM1, coord, LABEL_DIM3);
    } else                                                            // x-y plane
  if  ( (equal(dir,LABEL_DIM3) && DIMENSIONALITY > 1) || DIMENSIONALITY == 2 ) {
            grid1  = FieldObj::xgrid;  grid2  = FieldObj::ygrid;
            coord1 = FieldObj::xcoord; coord2 = FieldObj::ycoord;
            incr1  = 1;                incr2  = FieldObj::xsize; 
            num1   = FieldObj::xsize;  num2   = FieldObj::ysize;
            for (ix = 0; ix < field->xsize && si == -1; ix++)
				for (iy = 0; iy < field->ysize && si == -1; iy++)
					si = field->location_to_index(coord1[ix], coord2[iy], coord, 0);
			if (si == -1) field->location_to_index(coord1[ix], coord2[iy], coord, 1); // trigger error message
			else field_index = long(si);
			FieldObj::Grid->split(field_index, ix, iy, iz);
			field_index = iz * FieldObj::xysize;
			coord = FieldObj::zcoord[iz];

	  if (DIMENSIONALITY == 2) sprintf(win_title,"%s[%s,%s]", f->ID, LABEL_DIM1, LABEL_DIM2);
	      else sprintf(win_title,"%s[%s,%s,%.3g]", f->ID, LABEL_DIM1, LABEL_DIM2, coord);
  }
  else throw makeMessage("Cannot interpret direction \"%s\" in 2D plot",dir);
}


//*************************************************************************************

void FieldPlot2D::get_range()
{
	long   ind;
	double f;
	bool   initFlag = 1;
	fmin = fmax = 0.0;

	for (int j = 0; j < num2; j++)
	{
		for (int i = 0; i < num1; i++)  
		{
			ind = field_index + j * incr2 + i * incr1;
			if (field->ptype[ind] & VARY_MASK)
			{
				f = get_value(ind);
				if (initFlag)  {  initFlag = 0;  fmin = fmax = f;  }
				else if (f < fmin) fmin = f; 
				else if (f > fmax) fmax = f;
			}
		}
	}
	if ( (fabs( (fmax-fmin)/(fmax+fmin+1.0e-12)) < 1e-16 ) || (fmax <= fmin) )
		fmax = (fmin + 1e-8);
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//*******************************************************************************
//       B A S E   F I E L D   P L O T   1 D   I M P L E M E N T A T I O N
//*******************************************************************************

double FieldPlotObj::get_value(long ind)
   {
   double v = fptr[ind];
   long   pointType = field->ptype[ind];
   if ( !(pointType & VARY_MASK ) ) 
     //     fprintf(stderr,"fmin=%g \n",fmin);
     v = fmin; 
   if (!log_plot) return v;
      else return ( (v <= 0.0) ? 0.0 : log(v)/log(10.0) );
   }

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

FieldPlot1D::FieldPlot1D(FieldObj *f, char islog, const char *dir, double coord1, double coord2,
               const char *label) :  FieldPlotObj(f, islog, "", label)
{
  int    ix=0, iy=0, iz=0;
  signed long int si;

  if ( equal(dir, LABEL_DIM1) ) {
            grid = FieldObj::xgrid; coord = FieldObj::xcoord; 
            incr = 1;  num = FieldObj::xsize;
            while ( (si = field->location_to_index(coord[ix], coord1, coord2, 0)) == -1 && ix < field->xsize) ix++;
			if (si == -1) field->location_to_index(coord[ix], coord1, coord2, 1); // trigger error message
			else field_index = long(si);
			FieldObj::Grid->split(field_index, ix, iy, iz);
			field_index = iy * FieldObj::xsize + iz * FieldObj::xysize;
			coord1 = FieldObj::ycoord[iy];
            coord2 = FieldObj::zcoord[iz];

	    if (DIMENSIONALITY == 1) sprintf(win_title,"%s[%s]", f->ID, LABEL_DIM1);
	    else if (DIMENSIONALITY == 2) sprintf(win_title,"%s[%s,%.3g]", f->ID, LABEL_DIM1, coord1);
	    else sprintf(win_title,"%s[%s,%.3g,%.3g]", f->ID, LABEL_DIM1, coord1, coord2);
 } else
   if ( equal(dir, LABEL_DIM2) ) {
            grid = FieldObj::ygrid; coord = FieldObj::ycoord;
            incr = FieldObj::xsize; num = FieldObj::ysize;
            while ( (si = field->location_to_index(coord1, coord[iy], coord2, 0)) == -1 && iy < field->ysize) iy++;
			if (si == -1) field->location_to_index(coord1, coord[iy], coord2, 1); // trigger error message
			else field_index = long(si);
			FieldObj::Grid->split(field_index, ix, iy, iz);
			field_index = ix + iz * FieldObj::xysize;
			coord1 = FieldObj::xcoord[ix];
            coord2 = FieldObj::zcoord[iz];

	    if (DIMENSIONALITY == 2) sprintf(win_title,"%s[%.3g,%s]", f->ID, coord1, LABEL_DIM2);
	    else sprintf(win_title,"%s[%.3g,%s,%.3g]", f->ID, coord1, LABEL_DIM2, coord2);
 } else 
  if  ( equal(dir, LABEL_DIM3) ) {
            grid = FieldObj::zgrid;  coord = FieldObj::zcoord;
            incr = FieldObj::xysize; num = FieldObj::zsize;
            while ( (si = field->location_to_index(coord1, coord2, coord[iz], 0)) == -1 && iz < field->zsize) iz++;
			if (si == -1) field->location_to_index(coord1, coord2, coord[iz], 1); // trigger error message
			else field_index = long(si);
			FieldObj::Grid->split(field_index, ix, iy, iz);
			field_index = ix + iy * FieldObj::xsize;
			coord1 = FieldObj::xcoord[ix];
            coord2 = FieldObj::ycoord[iy];

	    sprintf(win_title,"%s[%.3g,%.3g,%s]", f->ID, coord1, coord2, LABEL_DIM3);
  }
  else throw makeMessage("Cannot interpret direction \"%s\" in 1D plot", dir);
}

//*************************************************************************************

void FieldPlot1D::get_range()
{
long   ind;
double f;
bool   flag = true;

  for (int j = 0; j < num; j++)  {
    ind = field_index + j * incr;
    if (field->ptype[ind] & VARY_MASK)  {
      if (flag) {
        fmin = get_value(field_index);
        fmax = get_value(field_index); 
        flag = false;
      }
      else {
        f = get_value(ind);
        if (f < fmin) fmin = f;  
          else if (f > fmax) fmax = f;
      }
	}
  }
    
  //fprintf(stderr,"fmin=%g fmax=%g \n",fmin,fmax);

  if ( (fabs((fmax - fmin) / (fmax+fmin+1.0e-12)) < 1.0e-16) || (fmax <= fmin) )
     fmax = fmin + 1e-8;
}


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//*******************************************************************************
//             B A S E   C L A S S   X M G R   P I P E   P L O T
//*******************************************************************************

 int    XmgrPlot::graph_num = 0;
 int    XmgrPlot::graph_count = 0;
 int    XmgrPlot::rows = 0;
 int    XmgrPlot::cols = 0;
 int    XmgrPlot::divs = 1;
 double XmgrPlot::xscale = 1;
 double XmgrPlot::yscale = 1;


 FILE *XmgrPlot::file = stdout;

//*******************************************************************************

  void XmgrPlot::init(TokenString &TS, int gnum, FILE *f, int rs, int cs)
    {
    if (TS.token2_count("xmgrace.style","landscape")) {
      fprintf(file, "@ PAGE LAYOUT LANDSCAPE \n");
      xscale = 1.2;
    } else 
    if (TS.token2_count("xmgrace.style","portrait")) {
      fprintf(file, "@ PAGE LAYOUT PORTRAIT \n");
      yscale = 1.2;
    } 

	double XMGR_XSCALE = 1.2;
    TS.get_param("XMGR_XSCALE", &XMGR_XSCALE);

    double rmargin = xmgr_rmargin;
    double lmargin = xmgr_lmargin;
    double tmargin = xmgr_tmargin;
    double bmargin = xmgr_bmargin;
    double dxmargin = xmgr_dxmargin;
    double dymargin = xmgr_dymargin;

    TS.get_param("xmgr.right",  &rmargin); //, "Right margin should be double precision");
    TS.get_param("xmgr.left",   &lmargin); //, "Left margin should be double precision");
    TS.get_param("xmgr.top",    &tmargin); //, "Top margin should be double precision");
    TS.get_param("xmgr.bottom", &bmargin); //, "Bottom margin should be double precision");
    TS.get_param("xmgr.xspace", &dxmargin); //, "Top margin should be double precision");
    TS.get_param("xmgr.yspace", &dymargin); //, "Bottom margin should be double precision");

    if ( !(graph_num = gnum) ) return;
    file = f;
    rows = rs; cols = cs;

    if (rows * cols < graph_num) {
      rows = int( sqrt(double(graph_num)) + 0.5 );
      cols = int( ceil( double(graph_num) / double(rows) ) );
    }
    
    divs = rows;
    if (cols > rows) divs = cols;  

    double dx = (1 - lmargin - rmargin - (cols - 1) * dxmargin) / double(cols); 
    double dy = (1 - tmargin - bmargin - (rows - 1) * dymargin) / double(rows);  

    TS.get_int_param("plot.update.steps", &XMGR_STEPS);

    //    fprintf(file, "@ version 40102 \n");
    //    fprintf(file, "@ page layout free \n");      
    // fprintf(file, "@ PAGE SIZE 900, 800\n");
    fprintf(file, "@ TIMESTAMP on \n");
    fprintf(file, "@ TIMESTAMP CHAR SIZE 0.6\n");

	for (int i = 0; i < rows; i++)
	{
		double y1 = 1 - i * (dy + dymargin) - tmargin;
		double y0 = y1 - dy;
		for (int j = 0; j < cols; j++)
		{ 
			double x0 = lmargin + j * (dx + dxmargin);
			double x1 = x0 + dx;
			int  graph = i * cols + j;
			if (graph >= graph_num) break;
			fprintf(file, "@with g%d\n", graph);
			fprintf(file, "@g%d on\n", graph);
			fprintf(file, "@ world ymin 0.000001\n");
			fprintf(file, "@ world ymax 1.000000\n");
			fprintf(file, "@ view xmin %g\n", x0 * xscale * XMGR_XSCALE );
			fprintf(file, "@ view xmax %g\n", x1 * xscale * XMGR_XSCALE );
			fprintf(file, "@ view ymin %g\n", y0 * yscale );
			fprintf(file, "@ view ymax %g\n", y1 * yscale );
		}
	}
	processStrings(TS);
  }
		

//*******************************************************************************

 XmgrPlot::XmgrPlot(int sets, char is_log, const char *xlabel)
  {
  graph_id = graph_count;

  if (is_log) logScale = true; 
  else logScale = false;

  if ( ++graph_count > graph_num )  
     throw makeMessage("XmgrPlot::XmgrPlot: too many graphs: [%d]>[%d]\n", graph_count, graph_num);

  set_num = sets;
  set_count = 0;

  double dx = (1 - xmgr_lmargin - xmgr_rmargin - (cols - 1) * xmgr_dxmargin) / double(cols); 
  double dy = (1 - xmgr_tmargin - xmgr_bmargin - (rows - 1) * xmgr_dymargin) / double(rows);  

  int row = graph_id / cols;
  int col = graph_id - row * cols;

  fprintf(file, "@with g%d\n", graph_id);
  fprintf(file, "@g%d on\n", graph_id);
  if (is_log) fprintf(file, "@g%d type logy\n", graph_id);
  else fprintf(file, "@g%d type xy\n", graph_id);

  // strcpy(x_label, xlabel);
 
  if (row + 1 == rows) {
    fprintf(file, "@ xaxis label \"%s\"\n", xlabel);
    fprintf(file, "@ xaxis label char size %g\n", 0.9 - 0.1 * (divs - 1) );
  }

  fprintf(file, "@    legend off\n");

  for (int i = 0; i < set_num; i++)
    {
    fprintf(file, "@ s%d linestyle 1\n", i);
    fprintf(file, "@ s%d linewidth 2\n", i);
    fprintf(file, "@ s%d color %d\n", i, setColor(i));
    fprintf(file, "@ s%d symbol size 0.8\n", i);
    }
 
  if (set_num > 1)
    {
    double ly = 1.0 - xmgr_tmargin - row * (dy + xmgr_dymargin) - dy_legend / rows;
    double lx = xmgr_lmargin + (col + 1) * dx + col * xmgr_dxmargin - dx_legend / cols;

    fprintf(file, "@    legend loctype view\n");
    fprintf(file, "@    legend layout 0\n");
    fprintf(file, "@    legend vgap 2\n");
    fprintf(file, "@    legend hgap 1\n");
    fprintf(file, "@    legend length 2\n");
    fprintf(file, "@    legend box on\n");
    fprintf(file, "@    legend box color 1\n");
    fprintf(file, "@    legend x1 %g\n", lx * xscale );
    fprintf(file, "@    legend y1 %g\n", ly * yscale );
    fprintf(file, "@    legend font 4\n");
    fprintf(file, "@    legend char size 0.8\n");
    fprintf(file, "@    legend color 1\n");                                                      
    }
  }

//*******************************************************************************

int XmgrPlot::setColor(int set) {

  const  int setColors[30] = 
   {2, 4, 10, 1, 9, 11, 13, 3, 8, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}; 

  return setColors[set];
}
//*******************************************************************************

void XmgrPlot::processStrings(TokenString &TS) {
  char   str[1024];
  double x=0.05, y=1.0, charSize = 0.8;
  int    sFont = 4, sColor= 4;

  for (int i=0; i< TS.token_count("plot.string"); i++) {
    y -= charSize * 0.03;
    TS.trail_pars("plot.string", i+1, 's', str, 'd', &x, 'd', &y, 'd', &charSize, 'i', &sFont, 'i', &sColor);
    fprintf(file, "@with string \n@string %g , %g \n@string char size %f \n", x * xscale, y * yscale, charSize);
    fprintf(file, "@string font %d \n@string color %d \n", sFont, sColor);
    fprintf(file, "@string on \n@string def \"%s\"\n", str);
  }
}


//*******************************************************************************
//               C L A S S   X M G R   P O I N T   P L O T
//*******************************************************************************

 XmgrPointPlot::XmgrPointPlot(int sets, char is_log, double T, const char *xlabel) : 
 XmgrPlot(sets, is_log, xlabel), PlotObj(0, 0, "", xlabel)
  {
  last = T;
  steps = XMGR_STEPS / cols;
  tScale = double( steps ) / last;

  tptrs  = new double *[sets];
  fptrs  = new double *[sets];
  x_old  = new double[sets];
  f_old  = new double[sets];
  x_temp = new double[sets];
  f_temp = new double[sets];

  pointCount = new int[sets];
  maxPoints  = new int[sets];
  tArray     = new double *[sets];
  fArray     = new double *[sets];

  fmin = 1e20; fmax = -1e20;
  }
  
//*******************************************************************************

void XmgrPointPlot::get_range()
  {
  for (int i = 0; i < set_num; i++)
      {
      if ( *fptrs[i] < fmin )  fmin = *fptrs[i];
      if ( *fptrs[i] > fmax )  fmax = *fptrs[i];   
      }

  if ( (fabs((fmax - fmin)/(fmax + fmin + 1.0e-12)) < 1.0e-16) || (fmax <= fmin) ) 
      fmax = fmin + 1e-8;
  }
 
//*******************************************************************************

 void XmgrPointPlot::set_set(double *ptr, double *tptr, const char *txt)
    {
    fptrs[set_count] = ptr;
    tptrs[set_count] = tptr;
    x_old[set_count] = x_temp[set_count] = -100.0;
    f_old[set_count] = f_temp[set_count] = 0.0;

    pointCount[set_count] = 0;
    maxPoints[set_count]  = XMGR_STEPS;

    tArray[set_count] = new double[maxPoints[set_count]];
    fArray[set_count] = new double[maxPoints[set_count]];
    
    strcat(win_title, txt);

    fprintf(file, "@with g%d\n", graph_id);
    fprintf(file, "@g%d on\n", graph_id);
    fprintf(file, "@ legend string %d \"%s\"\n", set_count, txt);

    set_count ++;

    if (set_count < set_num) strcat(win_title, ", ");
    else
      {
      first = *tptr;
      last += first;
      char format[30]; 
      char str[50];
      double gx = format_double(first, last, XMGR_DIVISIONS, 2, format);

      sprintf(str, format, first);  fprintf(file, "@ world xmin %s\n", str);
      sprintf(str, format, last);   fprintf(file, "@ world xmax %s\n", str);
      fprintf(file, "@ xaxis tick major %g\n", gx);
      fprintf(file, "@ xaxis tick minor %g\n", gx/2.0);
      fprintf(file, "@ xaxis ticklabel char size %g\n", 0.9 + 0.1 * (1 - divs));
      fprintf(file, "@ title \"%s vs. %s\"\n", win_title, x_label);
      fprintf(file, "@ title font 7\n");
      fprintf(file, "@ title size %g\n", 1.1 + 0.1 * (1 - rows) );
      fprintf(file, "@ subtitle font 4\n");
      fprintf(file, "@ subtitle size %g\n", 0.7 + 0.1 * (1 - rows) );
      fprintf(file, "@ title color %d\n", title_color);
      fprintf(file, "@ yaxis label \"%s\"\n", win_title);
      fprintf(file, "@ yaxis label char size %g\n", 0.9 + 0.1 * (1 - divs) );
      } 
    }

//*************************************************************************************
//                         X M G R   P L O T   D R A W
//*************************************************************************************


void  XmgrPointPlot::draw()
   {
   bool   redrawPlot = false, redrawFlag, newValue, drawTemp;
   bool   firstSet = true;
   double tiny = 1.0e-16;
   char   xtxt[40], mmform[40], mintxt[40], maxtxt[40], ytxt[40];
   char   format[80];

   for (int set = 0; set < set_num; set++)   {

     double Time  = *tptrs[set];
     double Val   = *fptrs[set];
     drawTemp = false;

     if (_isnan(Val)) globalError(StrCpy("Undefined (nan) value passed to Xmgr point plot"));

     if ( Time < x_old[set] ) { 
       if ( firstSet )  { fmin = 1e20; fmax = -1e20; }
       truncate(set);
     }
     else {
       redrawFlag = ( int( Time * tScale ) == int( x_old[set] * tScale) )        ? false : true;
       newValue   = ( fabs(f_old[set] - Val) < (fmax - fmin) * UPDATE_ACCURACY ) ? false : true;
       if ( !redrawFlag && !newValue ) { x_temp[set] = Time; f_temp[set] = Val; continue; }
       if (redrawFlag) redrawPlot = true;
       if (newValue && pointCount[set])
	     if (x_temp[set] > x_old[set])    // if we didn't draw this point before
            if ( fabs(f_temp[set] - Val) > (fmax - fmin) * UPDATE_ACCURACY ) { // and we deviated from it
              tArray[set][pointCount[set]] = x_temp[set];        // store it
              fArray[set][pointCount[set]] = f_temp[set];
              if (++pointCount[set] >= maxPoints[set]) addMemory(set);   
              drawTemp = true;
	     } 
     }

     double xDelta = fabs( Time - x_old[set] );
     //double yDelta = fabs( Val  - f_old[set] ); 

     tArray[set][pointCount[set]] = x_old[set] = Time;
     fArray[set][pointCount[set]] = f_old[set] = Val;
     if (++pointCount[set] >= maxPoints[set]) addMemory(set);   

     if ( firstSet ) 
       {
       get_range();
       
       double dy = format_double(fmin, fmax, XMGR_DIVISIONS, 2, format);
       double y0 = logScale ? fmin : dy * floor(fmin / dy); 
       double y1 = logScale ? fmax : dy * ceil(fmax / dy); 
       if ( logScale && !(y0 > 0) ) y0 = 1e-16;         
       if ( fabs( y1 - y0 ) < (tiny + dy) ) y1 = y0 + tiny + dy;
       
       format_double(Time-xDelta, Time, 1, 2, format);
       format_double(fmin, fmax, 1, 4, mmform);

       if (fmin == 0.0) strcpy(mintxt,"0"); else sprintf(mintxt, mmform, fmin);
       if (fmax == 0.0) strcpy(maxtxt,"0"); else sprintf(maxtxt, mmform, fmax);
       sprintf(xtxt, format,  Time);
       sprintf(ytxt,"%.5g",Val);

       fprintf(file, "@ WITH G%d\n", graph_id);
       fprintf(file, "@ G%d ON\n", graph_id);       
       fprintf(file, "@ subtitle \"%s at %s (min=%s max=%s)\"\n", ytxt, xtxt, mintxt, maxtxt);

       dy = format_double(y0, y1, XMGR_DIVISIONS, 2, mmform);
       sprintf(mintxt, mmform, y0);
       sprintf(maxtxt, mmform, y1);
       fprintf(file, "@ world ymin %s\n", mintxt);
       fprintf(file, "@ world ymax %s\n", maxtxt);
       fprintf(file, "@ yaxis tick major %g\n", dy);
       fprintf(file, "@ yaxis tick minor %g\n", dy*0.5);
       fprintf(file, "@ yaxis ticklabel char size %g\n", 0.9 + 0.1 * (1 - divs) );
       }
     firstSet = false;

     if (drawTemp) fprintf(file, "@ g%d.s%d POINT %.10g, %.10g \n", graph_id, set, x_temp[set], f_temp[set]);
     fprintf(file, "@ g%d.s%d POINT %.10g, %.10g \n", graph_id, set, Time, Val);
     x_temp[set] = Time; f_temp[set] = Val;
 
     /*{ sprintf(xtxt, format,  Time);
         format_double(Val-yDelta, Val, 1, 5, format);
         sprintf(ytxt, format, Val);
         fprintf(file, "@ g%d.s%d POINT %s, %s \n", graph_id, set, xtxt, ytxt ); }*/

     if ( (set + 1 == set_num) && (graph_id + 1 == graph_num) && redrawPlot && pointCount[set] > 1 )
       {
       fprintf(file, "@ redraw\n");
       fflush(file);
       }
      
     } // ** end loop over data sets
   }

//*************************************************************************************

void  XmgrPointPlot::truncate(int set)
   {
   fprintf(file, "@WITH G%d\n", graph_id);
   fprintf(file, "@KILL s%d\n",  set);
   fprintf(file, "@s%d color %d\n",  set, setColor(set));
   fprintf(file, "@s%d linewidth 2\n", set);

   int i = 0;
   double t, f;

   while ( ( tArray[set][i] < *tptrs[set] ) && ( i < pointCount[set] ) ) {
     t = tArray[set][i]; f = fArray[set][i];
     if ( !_isnan( f ) )  fprintf(file, "@ s%d POINT %.10g, %.10g \n", set, t, f );
     if ( f > fmax ) fmax = f;
     if ( f < fmin ) fmin = f;
     i++;
   }

   pointCount[set] = i;
   }

//*************************************************************************************

void  XmgrPointPlot::addMemory(int set)
   {
    int maxOld = maxPoints[set];
    int maxNew = maxPoints[set] += 400;
    double *tArrayNew = new double[maxNew];
    double *fArrayNew = new double[maxNew];

    for (int i = 0; i < maxOld; i++) 
      { tArrayNew[i] = tArray[set][i]; fArrayNew[i] = fArray[set][i]; }
 
    delete [] tArray[set];   delete [] fArray[set];
    tArray[set] = tArrayNew; fArray[set] = fArrayNew; 
   }


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//*******************************************************************************
//               C L A S S   1 D   X M G R   P I P E   P L O T
//*******************************************************************************

Xmgr1Dplot::Xmgr1Dplot(FieldObj *f, char islog, const char *dir, double coord1, double coord2, 
                       double T, int sets) : XmgrPlot(sets, islog),
            FieldPlot1D(f, 0, dir, coord1, coord2)
{

  x_value = -100.0;
  last    = T;
  steps   = UPDATE_STEPS_1D;

  for (int set = 1; set < set_num; set++)  setTime[set] = x_value;

  fprintf(file, "@ title \"%s\"\n", win_title);
  fprintf(file, "@ title font 7\n");
  fprintf(file, "@ title size %g\n", 1.1 + 0.1 * (1 - rows) );
  fprintf(file, "@ subtitle font 4\n");
  fprintf(file, "@ subtitle size %g\n", 0.7 + 0.1 * (1 - rows) );
  fprintf(file, "@ title color %d\n", title_color);
  fprintf(file, "@ yaxis label \"[%s] (\\8m\\4M)\" \n", field->ID);
  fprintf(file, "@ yaxis label char size %g\n", 0.9 + 0.1 * (1 - divs) );
  fprintf(file, "@ xaxis label \"distance (\\8m\\4m)\" \n");
  fprintf(file, "@ xaxis label char size %g\n", 0.9 + 0.1 * (1 - divs) );
  fprintf(file, "@ xaxis ticklabel char size %g\n", 0.9 + 0.1 * (1 - divs));
  fprintf(file, "@ yaxis ticklabel char size %g\n", 0.9 + 0.1 * (1 - divs));

}

//*******************************************************************************

int Xmgr1Dplot::setColor(int set) {

 const  int setColors[30] = 
   {2, 2, 11, 5, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7}; 

 return setColors[set];
}

//*************************************************************************************
//                     1 D   X M G R   P L O T   D R A W
//*************************************************************************************

void  Xmgr1Dplot::draw()
   {
   bool   empty = true;
   double tScale = double(steps) / last;
   int    set;
   
   if ( field->Time < x_value )                   // pop the set stack
     while (setTime[set_num-1] > field->Time && setTime[set_num-2] < setTime[set_num-1])
       for (set = set_num-1; set > 0; set--) {
         fprintf(file, "@ move g%d.s%d to g%d.s%d \n", graph_id, set-1, graph_id, set);
         setTime[set] = setTime[set-1];
       }
   else if  ( int( field->Time * tScale + 1e-20) == int( x_value * tScale  + 1e-12) ) return;

   FieldPlot1D::get_range();

   fprintf(file, "@WITH G%d\n", graph_id);
   fprintf(file, "@G%d ON\n", graph_id);
   fprintf(file, "@ subtitle \"time = %gms\"\n", field->Time);   
   
   for (set = 1; set < set_num; set++) {                    // push set stack
     fprintf(file, "@ move g%d.s%d to g%d.s%d \n", graph_id, set, graph_id, set-1 );
     
     setTime[set-1] = setTime[set];
   }
   setTime[set_num-1] = x_value = field->Time;

   for (set = 0; set < set_num; set++) {
     fprintf(file,"@ s%d color %d\n", set, Xmgr1Dplot::setColor(set_num-1-set) ); 
     fprintf(file,"@ s%d linewidth 1\n", set);
     fprintf(file,"@ legend string %d \"t=%gms\" \n", set, setTime[set]);
   }
   fprintf(file,  "@ s%d linewidth 2\n", set_num-1);
   
   double y;
   int    iLeft = 0, iRight = num-1;

   while ( !(field->ptype[field_index+iLeft *incr] & VARY_MASK) ) iLeft++;
   while ( !(field->ptype[field_index+iRight*incr] & VARY_MASK) ) iRight--;

   for (int j = iLeft; j <= iRight; j++) 
       if ( !_isnan( y = get_value(field_index + incr * j) ) ) {
         empty = false;
         fprintf(file, "@ g%d.s%d POINT %.10g, %.10g \n", graph_id, set_num-1, coord[j], y);
       }   
       else globalError(StrCpy("Undefined (nan) value passed to Xmgr 1D plot"));

   if ( !empty ) {  fprintf(file, "@ s%d on\n", set_num-1);
                    fprintf(file, "@ autoscale\n");          }

   if (graph_id + 1 == graph_num)
         {
         fprintf(file, "@ redraw\n");
         fflush(file);
         }

   }   

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//*******************************************************************************
//               C L A S S   2 D   X M G R   P I P E   P L O T
//*******************************************************************************

Xmgr2Dplot::Xmgr2Dplot(FieldObj *f, char islog, const char *dir, double coord, double T, double theta, double factor, int nBins) 
	      : XmgrPlot(0, islog, ""), FieldPlot2D(f, islog, dir, coord)
 {

	 Theta        = theta;
	 FscaleFactor = factor;
	 nBinsTotal   = nBins;
  
  x_value = -100.0;
  last    = T;
  steps   = UPDATE_STEPS_1D;

  fprintf(file, "@ title \"%s\"\n", win_title);
  fprintf(file, "@ title font 7\n");
  fprintf(file, "@ title size %g\n", 1.1 + 0.1 * (1 - rows) );
  fprintf(file, "@ subtitle font 4\n");
  fprintf(file, "@ subtitle size %g\n", 0.7 + 0.1 * (1 - rows) );
  fprintf(file, "@ title color %d\n", title_color);
  
  fprintf(file, "@ yaxis label \"%s\" \n", LABEL_DIM1 );
  fprintf(file, "@ yaxis label char size %g\n", 0.0); // 0.9 + 0.1 * (1 - divs) );
  fprintf(file, "@ yaxis label \"%s\" \n", LABEL_DIM2 );
  fprintf(file, "@ xaxis label char size %g\n", 0.0); // 0.9 + 0.1 * (1 - divs) );
  
  fprintf(file, "@ xaxis tick major size 0\n");
  fprintf(file, "@ xaxis tick minor size 0\n");
  fprintf(file, "@ yaxis tick major size 0\n");
  fprintf(file, "@ yaxis tick minor size 0\n");
  
  fprintf(file, "@ xaxis ticklabel char size %g\n", 0.0); //0.9 + 0.1 * (1 - divs));
  
   cosine = cos(Theta), sine = sin(Theta);

   Xmax     = cosine * (coord2[num2-1] - coord2[0]) + (coord1[num1-1] - coord1[0]);
   Ymax     =   sine * (coord2[num2-1] - coord2[0]) * (1 + FscaleFactor);
   Xscale   = 0.999999        / (Xmax + 1e-12);
   Yscale   = 0.999999 * sine / (Ymax + 1e-12);
   
   binMult  = 1.0 + cosine * (coord2[num2-1] - coord2[0]) / (coord1[num1-1] - coord1[0]);
   nBinsX   = int( double(nBinsTotal) / binMult + 0.5 ); 
   binScale = 1.0 / double(nBinsX - 1);
   
   // nBins = nBins / (Xscale * (coord1[num1-1] - coord1[0]));
   Ybuffer = new double[nBinsTotal + 1];
}

//*************************************************************************************
//                    2 D   X M G R   P L O T   D R A W
//*************************************************************************************

void  Xmgr2Dplot::draw()
   {
   double tScale = double(steps) / last;
   long   ind;
   int    i0,  xBin;
   double Fscale;
   double X0, X1, F0, F1, interpolX, interpolY;

   if  ( int( field->Time * tScale + 1e-20) == int( x_value * tScale  + 1e-12) ) return; 

   FieldPlot2D::get_range();
   fprintf(file, "@WITH G%d\n", graph_id);
   // fprintf(file, "@G%d ON\n", graph_id);
   fprintf(file, "@ subtitle \"time = %gms min(%s)=%g max(%s)=%g\"\n", field->Time, field->ID, fmin, field->ID, fmax);  

   Fscale = FscaleFactor * (coord2[num2-1] - coord2[0]) / (fmax - fmin + 1e-12);

   for (int i = 0; i < nBinsTotal; i++)  Ybuffer[i] = -100.0;

   for (int j = 0; j < num2; j++) 
   {
		fprintf(file, "@KILL s%d\n",         j   );
		fprintf(file, "@s%d color %d\n",     j, 2);
		fprintf(file, "@s%d linewidth %d\n", j, 1);

	   for (int i = 0; i < nBinsX; i++) {

		   interpolX = coord1[0] + i * binScale * (coord1[num1-1] - coord1[0]);
		   i0  = int(i * (num1 - 1) * binScale);
		   ind = field_index + j * incr2 + i0 * incr1;
		   F0  = get_value(ind);

		   if (i0 == num1 - 1) interpolY = F0;
		   else
		   {
			   F1 = get_value(ind + incr1);
			   X0 = coord1[i0];  X1 = coord1[i0+1];
			   interpolY = F0 + (interpolX - X0) * (F1 - F0)/(X1 - X0);
		   }

		   double XX = Xscale * (cosine *  (coord2[j] - coord2[0]) + (interpolX - coord1[0]) );
		   double YY = Yscale * ((coord2[j] - coord2[0]) + (interpolY - fmin) * Fscale );

		   xBin = int(XX * nBinsTotal);
		   if (YY < Ybuffer[xBin] ) YY = Ybuffer[xBin]; else Ybuffer[xBin] = YY;

		   fprintf(file, "@ s%d POINT %.10g, %.10g \n", j, XX, YY );
	   }

   }
  
   //fprintf(stderr, " (%d, %d) ", graph_num, graph_id);
   if (graph_id + 1 == graph_num)
         {
         fprintf(file, "@ redraw\n");
         fflush(file);
         }

   }   


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//*************************************************************************************
//                       U T I L I T Y   R O U T I N E S
//*************************************************************************************

double format_double(double number0, double number1, int divisions, int sigdigits, char *fstr)
{
const  int maxzeros = 4;
double dx, delta;
int    digits = 0, digitsBase = 0, zeros;

   strcpy(fstr,"%.");
   char sigdig[20];
   strcpy(sigdig,"");

   dx = fabs(number1 - number0) / double(divisions);
   delta = format_digits( dx, digits );
   double base = fabs(number1) > fabs(number0) ? fabs(number1) : fabs(number0);
   format_digits(base, digitsBase);

   if (dx < 1) {
     zeros = base < 1 ? digitsBase - 1 : 0;
     if (zeros <= maxzeros) sprintf(sigdig, "%df", digits + sigdigits - 1);
     else sprintf(sigdig, "%de", digits + sigdigits - zeros - 2);
   } 
   else {
     zeros = digits - sigdigits;
     if (zeros <= maxzeros) {
        int afterDec =  zeros < 0 ? -zeros : 0 ;
        sprintf(sigdig, "%df", afterDec);
     }
     else sprintf(sigdig, "%de", digitsBase - zeros - 1); 
   }

  strcat(fstr, sigdig);

  return delta;
}
//*************************************************************************************
// Returns the number of digits in "dx", before or after the decimal point

double format_digits(double dx, int &digits)
{
double mult = 1;
int    grid;
 double tiny = 1e-8;

   digits = 0;

   if (dx < 1) 
     { 
     while ( dx * mult + tiny < 1 && dx * mult > 0.0 ) { mult *= 10; digits++; } 
     grid = int(dx * mult + 0.5);
     if ((grid & 1) && (grid != 1)  && (grid != 5)) grid--;  // prefer even grid
     dx = double(grid) / double(mult); 
     }
   else if (dx >= 1)
     { 
     while ( dx / mult >= 10) { mult *= 10; digits++; }
     grid = int(dx / mult + 0.5);
     if ((grid & 1) && (grid != 1)  && (grid != 5)) grid--;   // prefer even grid
     dx = double(grid) * double(mult); 
     digits++;
     }

   return dx;
}

//*************************************************************************************
//   T O K E N - S T R I N G   C O N S T R U C T O R   F O R   P L O T   A R R A Y
//*************************************************************************************
  
int PlotArray::get_plot_num(TokenString &TS)
{
	int graphs = 0;

	int Total  = TS.token_count("plot") + TS.token_count("Export");

	int mutes  = TS.token2_count("plot","mute")    + TS.token2_count("plot","mute.log")    +
				 TS.token2_count("plot","1D.mute") + TS.token2_count("plot","1D.mute.log") +
				 TS.token2_count("plot","2D.mute") + TS.token2_count("plot","2D.mute.log") +
				 TS.token2_count("plot","dump")    + TS.token2_count("plot","binary")      + 
				 TS.token_count ("Export");
	int n1D    = TS.token2_count("plot","1D")      + TS.token2_count("plot","1D.log");
	int n2D    = TS.token2_count("plot","2D")      + TS.token2_count("plot","2D.log");

	int npoint = Total - mutes - n1D - n2D;

	char smethod[10];
	strcpy(smethod,"");


	if (TS.token_count(LOOP_TOKEN)) { 
		graphs = 0; 
		method = METHOD_MUTE; 
	}
	else  {
		if ( TS.assert("plot.method","xmgr") ) {  
			int rows = 0, cols = 0;
			if (TS.token2_count("plot.method","xmgr"))
				TS.trail_pars("plot.method", 1, 0,0, 'i', &rows, 'i', &cols);
			else
				TS.trail_pars( TS.token3_index("plot.method", "=", "xmgr"), 'i', &rows, 'i', &cols);
			method = METHOD_XMGR;
			strcpy(smethod,"Xmgr");
			graphs = npoint + n1D + n2D;
			XmgrPlot::init(TS, graphs, stdout, rows, cols);
		}
		else method = METHOD_MUTE;
	}

	if (TS.token_count("plot.print")) {
		long pos;
		mutes += n1D + n2D;
		if ( method != METHOD_XMGR ) mutes += npoint;
		else for(int i=1; i<=TS.token_count("plot"); i++) 
			if (!equal_( TS[ pos = TS.token_index("plot", i) + 1 ], "1D")     &&
				!equal_( TS[ pos = TS.token_index("plot", i) + 1 ], "2D")     &&
				!equal_( TS[ pos = TS.token_index("plot", i) + 1 ], "dump")   &&
				!equal_( TS[ pos = TS.token_index("plot", i) + 1 ], "binary") &&
				!equal_( TS[ pos = TS.token_index("plot", i) + 1 ], "mute") ) 
				mutes += TS.tokens_to_eol(pos);
	}


	int total = graphs + mutes;

	if (VERBOSE) 
		fprintf(stderr,"\n### Setting up %d plots (%d %s graphs plus %d disc dumps):\n", 
		total, graphs, smethod, mutes);

	return  total;
}

//*****************************************************************************
//                      S T A T U S      W I D G E T
//*****************************************************************************

  RunStatusString::RunStatusString(int length, const char *s1, double *p1, double T, const char *s2, double *p2)
  {
    widgetLength = length;
    if (widgetLength < 3) widgetLength = 1;
    widgetStr = new char[length+1];
    timeStr = StrCpy(s1);   
    timePtr = p1; 
    realTime = double( clock() / CLOCKS_PER_SEC );
    if (s2) 
      { extraStr = StrCpy(s2);  extraPtr = p2;  wholeStr = new char[length+strlen(s1)+strlen(s2)+60];}
    else { wholeStr = new char[length+strlen(s1)+60]; extraStr = 0; extraPtr = 0; }
    reset(T);
    //update();
  };

//*****************************************************************************

  RunStatusString::~RunStatusString()
  {
    fprintf(stderr,"\n");
    delete [] widgetStr; delete [] wholeStr; 
    delete [] timeStr; if (extraStr) delete [] extraStr;
  };

  
//*****************************************************************************

  void RunStatusString::reset(double T)
  {
    widgetCount = 0;
    strcpy(widgetStr,"");
    for (int i = 0; i < widgetLength; i++) strcat(widgetStr,"_");
    if (timePtr) {
      t0 = *timePtr;
      if (T > 0) total = T - t0;
    }
    strcpy(wholeStr, "");
  }    

//*****************************************************************************

  void RunStatusString::update(char c)
  {
    int i;

	double newTime = double( clock() / CLOCKS_PER_SEC );

    for (i = strlen(wholeStr); i > 0; i--) fprintf(stderr,"\b");

    if (widgetLength >= 3 && c != 0) {
        if (widgetCount < widgetLength) widgetStr[widgetCount++] = c;
        else {
          for (i = 1; i < widgetLength ; i++ ) widgetStr[i-1] = widgetStr[i];
          widgetStr[widgetLength - 1] = c;
        }
     }

    if (newTime < realTime + 1) return;
    realTime = newTime;

	if (widgetLength < 3) {
        char clockchar[4] = { '-', '\\', '|', '/' };
        widgetCount = (widgetCount + 1) % 4;
        widgetStr[0] = clockchar[widgetCount];
     }

	if (extraStr) 
      sprintf(wholeStr," [%s] (%d %%) %s=%.6g, %s=%.3g       ", widgetStr, int(100*(*timePtr-t0)/total + 0.5), 
	 	          timeStr, *timePtr, extraStr, *extraPtr);
    else if (timePtr) 
       sprintf(wholeStr," [%s] (%d %%) %s=%.6g       ", widgetStr, int(100*(*timePtr-t0)/total + 0.5),
                          timeStr, *timePtr ); 
    else
       sprintf(wholeStr,"%s", widgetStr);    

    fprintf(stderr,"%s",wholeStr); 
	fflush(stderr);
  }


//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//*************************************************************************************

  PlotArray::PlotArray(SimulationObj &SO)
  {
	  long     POS;
	  char     prefix[128], fileName[2048];  
	  double   SimTime = SO.totalSimTime;
	  int      vcount = 0;  // counter for the verbose comment print


	  count = 0; 

	  TokenString *params = SO.Params;
	  plot_num = get_plot_num( *params );

	  if (!plot_num) return;
	  array   = new PlotObj *[plot_num];

	  params->get_int_param("plot.steps.point",     &PlotObj::UPDATE_STEPS);
	  params->get_int_param("plot.steps.binary",    &PlotObj::UPDATE_STEPS_BINARY);
	  params->get_int_param("plot.steps.1D",        &PlotObj::UPDATE_STEPS_1D);
	  params->    get_param("plot.update.accuracy", &PlotObj::UPDATE_ACCURACY);

	  strcpy(fileName,"");
	  strcpy(prefix,"temp");
	  if ( params->token_count("plot.print", &POS) )  params->line_string(POS + 1, prefix);

	  //*************** Separate block for state export plot ****************************

	  for (int ii = 1; ii <= params->token_count("Export"); ii++) {
		  double tDump = SimTime;
		  params->trail_pars("Export", ii, 'd', &tDump, 's', fileName);
		  array[count++] = new StateDump(&SO, tDump, fileName);
		  if (VERBOSE)  
			  fprintf(stderr," plot #%d: State variable dump at t=%g into file \"%s\"\n", count, tDump, fileName);
		  vcount++;
	  }

	  //*********************************************************************************

	  for (int i = 1; i <= params->token_count("plot"); i++) 
	  try
	  {
		  char     ptype[100], VarID[200], log_plot;

		  POS = params->token_index("plot", i) + 1; 

		  params->get_string(POS, ptype); 
		  int pp = strlen( ptype );
		  if ( equal( ptype+pp-4, ".log" ) ) { log_plot = 1; ptype[pp-4] = '\0'; }
		  else if  ( equal( ptype, "log" ) ) { log_plot = 1; strcpy(ptype,"point"); }
		  else log_plot = 0;

		  if ( equal(ptype, "point") || equal(ptype, "mute") || SO.ResolveID(ptype) )
		  {  
			  double *time_ptr = 0, *kin_ptr = 0;
			  int     set_num  = 1,  xmgr_plot = 0;

			  if ( SO.ResolveID(ptype) ) POS--;

			  if ( ( method == METHOD_XMGR ) && !equal(ptype, "mute") )
			  {
				  set_num = params->tokens_to_eol(POS+1);
				  xmgr_plot = count;
				  array[count++] = new XmgrPointPlot(set_num, log_plot, SimTime);
			  }

			  //******************  MULTIPLE SETS IF XMGR POINT PLOT

			  for (int set = 0; set < set_num; set++)
			  { 
				  strcpy(VarID, "(null)");
				  POS += params->trail_pars(POS, 'S', VarID, 'E');
				  if ( ! ( kin_ptr = SO.ResolveID( VarID, &time_ptr) ) )
					  params->errorMessage(POS, makeMessage("Undefined variable \"%s\" in plot statement", VarID) );

				  if ( ( method == METHOD_XMGR ) && !equal(ptype, "mute") ) 
					  ((XmgrPointPlot *)array[xmgr_plot])->set_set(kin_ptr, time_ptr, VarID);

				  if ( params->token_count("plot.print") || equal(ptype,"mute") )
				  {
					  if ( equal(ptype,"mute") ) params->line_string(POS + 1, fileName);
					  else sprintf(fileName,"%s%s", prefix, VarID);
					  array[count++] = new MutePointPlot(kin_ptr, time_ptr, log_plot, SimTime, fileName, VarID);
				  }
			  }  //@@@@@@@@@@@@@@@@@  END SET CYCLE OVER POINT PLOT SETS @@@@@@@@@@@@@@@@@@@

		  }  else {  // not a point plot

			  FieldObj *field = 0;
			  double   coord1=0, coord2=0;
			  char     dir[MAX_TOKEN_LENGTH];
			  strcpy(fileName,"");     

			  strcpy(VarID, "(null)");
			  POS += params->trail_pars(POS, 'S', VarID, 'E');
			  if ( !( field = SO.ResolveField(VarID) ) ) 
				  params->errorMessage(POS, makeMessage("Undefined field \"%s\" in plot statement",VarID) );

			  if ( equal_(ptype,"2D") ) {
				  if (DIMENSIONALITY < 2) throw StrCpy("Cannot make a 2D plot of a 1-dimensional field");
				  strcpy(dir, "z"); // default direction for 2D plots in 2D geometry 
				  coord1 = 0.0;
				  if (DIMENSIONALITY > 2) POS += params->trail_pars(POS, 'S', dir, 'd', &coord1, 'E');
				  if ( equal(ptype, "2D.mute") || params->token_count("plot.print") )
				  {
					  double tt = SimTime;
					  if ( equal(ptype,"2D.mute") )  { 
						  params->trail_pars(POS, 'd', &tt, 'e'); //'s', fileName, 'E');
						  params->line_string(POS+1, fileName);								
					  }
					  array[count] = new MutePlot2D(field, log_plot, dir, coord1, tt, fileName, VarID);
					  if ( !equal(ptype,"2D.mute") ) { sprintf(fileName, "%s%s", prefix, array[count]->win_title);
					  strcpy( ((MutePlot2D *)array[count])->fileName, fileName); }
					  count++;
				  } 
				  if ((method == METHOD_XMGR) && equal(ptype,"2D"))
				  {
					  double theta = 1.1, factor = 0.4;
					  int bins = 400;
					  params->trail_pars(POS, 'd', &theta, 'd', &factor, 'i', &bins, 'E');
					  array[count++] = new Xmgr2Dplot(field, log_plot, dir, coord1, SimTime, theta, factor, bins);
				  }
			  }
			  else if ( equal(ptype,"dump") )
			  {
				  double tt = SimTime;
				  params->trail_pars(POS, 'd', &tt, 'e'); //'s', fileName);
				  params->line_string(POS+2, fileName);
				  array[count++] = new FieldDump(field, tt, fileName);
			  }
			  else if ( equal(ptype,"binary") )
			  {
				  params->line_string(POS+1, fileName);
				  //params->trail_pars(POS, 's', fileName);
				  array[count++] = new FieldDumpT(field, SimTime, fileName);
			  } 
			  else if ( equal_(ptype,"1D") )
			  {

				  strcpy(dir, LABEL_DIM1);  // default direction for 1D plots in 1D geometry
				  coord1 = coord2 = 0;
				  if      (DIMENSIONALITY == 3) POS += params->trail_pars(POS, 'S', dir, 'd', &coord1, 'd', &coord2, 'E');
				  else if (DIMENSIONALITY == 2) POS += params->trail_pars(POS, 'S', dir, 'd', &coord1, 'E');

				  if ((method == METHOD_XMGR) && equal(ptype,"1D"))
				  {
					  int sets = 5;
					  params->trail_pars(POS, 'i', &sets, 'E');
					  array[count++] = new Xmgr1Dplot(field, log_plot, dir, coord1, coord2, SimTime, sets);
				  } 
				  if ( params->token_count("plot.print") || equal(ptype,"1D.mute")) 
				  {
					  if (  equal(ptype,"1D.mute") )   params->line_string( POS+1, fileName ); //params->get_string( POS+1, fileName );
					  array[count] = new MutePlot1D(field, log_plot, dir, coord1, coord2, SimTime, fileName, VarID); // Use class constructor to create fileName
					  if ( !equal(ptype,"1D.mute") ) { sprintf(fileName, "%s%s", prefix, array[count]->win_title);
					  strcpy( ((MutePlot1D *)array[count])->fileName, fileName); }
					  count++;
				  }
			  } // endif 1D plot
			  else
			  {         // ignore some plots depending on plot_method
				  fprintf(stderr," *** ignoring \"%s\" plot\n", ptype);
				  continue;
			  }
		  } // end if not point plot

		  if (VERBOSE) for(; vcount < count; vcount++)
		  {
			  char s[5];
			  if (array[vcount]->log_plot) strcpy(s,"log "); else strcpy(s,"");
			  fprintf(stderr," plot #%d: %splot of %s\n", vcount+1, s, array[vcount]->win_title);
		  }

	  } catch (char *str) 
	  { 
		  params->errorMessage( params->token_index("plot", i) + 1, str, "Invalid plot statement"); 
	  }
	  catch (int m)
	  { 
		  params->errorMessage( params->token_index("plot", i) + 1, 0,   "Invalid plot statement"); 
	  }
	  // ****************   end loop over plot number
  }

//*************************************************************************************
//*************************************************************************************
  
PlotArray::~PlotArray()  { 

  if (!count) return;
  for (int i = 0; i < count; i++) delete array[i]; 
  delete [] array;

}

//*************************************************************************************
//*************************************************************************************
