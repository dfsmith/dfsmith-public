#include <stdio.h>
#include <math.h>

const double pi=3.14159265358979323;

typedef struct {
	double ox,oy;
} context;

typedef struct {
	double x,y;
} coord;
#define XY(C) C.x,C.y
#define LT "stroke=\"black\" stroke-width=\"0.1\" fill=\"none\""

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

static void start(void) {
	printf("<svg width=\"7.5in\" height=\"7.5in\" viewBox=\"-100 -100 200 200\">\n");
}

static void stop(void) {
	printf("</svg>\n");
}

static void cross(coord o,double w) {
	printf("<path "LT" d=\"M%f %f m%f %f l%f %f m%f %f l%f %f\"/>\n",
		XY(o), -w,0.0, 2*w,0.0, -w,-w, 0.0,2*w);
}

static void circle(coord o,double r) {
	printf("<circle "LT" cx=\"%f\" cy=\"%f\" r=\"%f\"/>\n",XY(o),r);
}

static void text(coord o,double theta,const char *label) {
	if (!label[0]) return;
	printf("<text x=\"%f\" y=\"%f\" rotate=\"%f\" font-size=\"6\">%s</text>\n",
		XY(o),-360*theta/(2*pi),label);
}

static void box(double r,double theta,double wtheta,const char *label) {
	coord v[4];
	double htheta,hr;
	
	htheta=wtheta/2;
	hr=r*htheta;
	v[0]=rt(r-hr,theta-htheta);
	v[1]=rt(r+hr,theta-htheta);
	v[2]=rt(r+hr,theta+htheta);
	v[3]=rt(r-hr,theta+htheta);
	printf("<g>\n");
	printf("<path "LT" d=\"M%f %f L%f %f L%f %f L%f %f Z\"/>\n",
		XY(v[0]),XY(v[1]),XY(v[2]),XY(v[3]));
	
	circle(rt(r,theta),1);
	cross(rt(r,theta),2);
	
	for(theta-=0.8*htheta;*label;label++,theta+=htheta/2) {
		char s[2];
		s[0]=*label;
		s[1]='\0';
		text(rt(r+0.8*hr,theta),theta,s);
	}
	printf("</g>\n");
}

int main(void) {
	const double scale=25;
	double inner=2.40,outer=3.00;
	double lp=29.53;
	int i;

	start();

	circle(xy(0,0),scale*3.75);
	//circle(xy(0,0),scale*inner);
	//circle(xy(0,0),scale*outer);
	circle(xy(0,0),scale*0.1);
	cross(xy(0,0),10);
	
	for(i=0;i<31;i++) {
		char date[6];
		sprintf(date,"%d",1+i);
		box((inner + (outer-inner)*i/lp)*scale,
			2*pi*i/lp,
			2*pi/lp,
			date);
	}

	stop();
	return 0;
}
