/* > lunar.c */
/* Daniel F. Smith, 2017 */
/* Compile with
 * gcc -Wall lunar.c -o lunar -lm && ./lunar | rsvg-convert -f pdf -o lunar.pdf
 */
#include <stdio.h>
#include <math.h>

#define SPIRAL 0	/* use a spiral for the date ring */
#define TWOLAYER 1	/* use a staggered date ring */

const double pi=3.14159265358979323;

typedef struct {
	double ox,oy;
} context;

typedef struct {
	double x,y;
} coord;
#define XY(C) C.x,C.y
#define LT "stroke=\"black\" stroke-width=\"1\" fill=\"none\""

static coord rt(double r,double theta) {
	coord ret;
	ret.x=r*sin(theta);
	ret.y=r*cos(theta);
	return ret;
}

static coord xy(double x,double y) {
	coord ret;
	ret.x=x;
	ret.y=y;
	return ret;
}

static void start(double width_in,double height_in,double perinch) {
	printf("<svg width=\"%fin\" height=\"%fin\" "
		"viewBox=\"%f %f %f %f\">\n",
		width_in,height_in,
		perinch*width_in*-0.5,perinch*height_in*-0.5,
		perinch*width_in,     perinch*height_in);
}

static void end(void) {
	printf("</svg>\n");
}

static void startgroup(void) {
	printf("<g>\n");
}

static void endgroup(void) {
	printf("</g>\n");
}

static void cross(coord o,double w) {
	printf("<path "LT" d=\"M%f %f m%f %f l%f %f m%f %f l%f %f\"/>\n",
		XY(o), -w,0.0, 2*w,0.0, -w,-w, 0.0,2*w);
}

static void circle(coord o,double r) {
	printf("<circle "LT" cx=\"%f\" cy=\"%f\" r=\"%f\"/>\n",XY(o),r);
}

static void crosshair(coord o,double r) {
	startgroup();
	circle(o,r);
	cross(o,r*2);
	endgroup();
}

static void text_cen(coord o,double theta,double size,const char *label) {
	if (!label[0]) return;
	printf("<text text-anchor=\"middle\" x=\"%f\" y=\"%f\" transform=\"rotate(%f,%f,%f)\" font-size=\"%f\">%s</text>\n",
		XY(o),-360*theta/(2*pi),XY(o),size,label);
}

static void rtbox(double r,double theta,double wtheta,const char *label) {
	coord v[4];
	double htheta,hr;
	
	htheta=wtheta/2;
	hr=r*htheta;
	v[0]=rt(r-hr,theta-htheta);
	v[1]=rt(r+hr,theta-htheta);
	v[2]=rt(r+hr,theta+htheta);
	v[3]=rt(r-hr,theta+htheta);
	
	startgroup();
	printf("<path "LT" d=\"M%f %f L%f %f L%f %f L%f %f Z\"/>\n",
		XY(v[0]),XY(v[1]),XY(v[2]),XY(v[3]));
	
	crosshair(rt(r,theta),5);
	text_cen(rt(r+0.8*hr,theta),theta,24,label);
	endgroup();
}

int main(void) {
	const double scale=100;
	double inner=2.80,outer=3.40;
	double lp=29.53; /* lunar period for earthly observer */
	int i;

	start(8.5,11,scale);
	text_cen(xy(0,scale*(-1-outer)),0,36,"Lunar Calendar Template");

	/* boudaries */
	circle(xy(0,0),scale*3.75);
	circle(xy(0,0),scale*2.25);
	//circle(xy(0,0),scale*inner);
	//circle(xy(0,0),scale*outer);

	crosshair(xy(0,0),0.1*scale);
	
	startgroup();
	for(i=0;i<31;i++) {
		char date[6];
		double r;
		
		#if SPIRAL
		r=(inner + (outer-inner)*i/lp);
		#endif
		#if TWOLAYER
		r=((i<29)?inner:outer);
		#endif
		
		sprintf(date,"%d",1+i);
		rtbox(r*scale,2*pi*i/lp,2*pi/lp,date);
	}
	endgroup();

	end();
	return 0;
}
