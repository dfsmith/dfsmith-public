#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define SWAP(t,x,y) {t z; z=(x); (x)=(y); (y)=z;}

static double rhtoah(double rhpc,double degC) {
	/* rhpc is relative humidity, in percent */
	/* degC is the temperature in degrees Celsius */
	const double c=2.16679;
	const double tc=647.096;
	const double tt=273.16;
	const double pc=22064000;
	const double c1=-7.85951783;
	const double c2=1.84408259;
	const double c3=-11.7866497;
	const double c4=22.6807411;
	const double c5=-15.9618719;
	const double c6=1.80122502;
	double pw,pws,K,eta,coeff;
	
	K=degC+tt;
	eta=1.0-(K/tc);
	coeff =c1*eta;
	coeff+=c2*pow(eta,1.5);
	coeff+=c3*pow(eta,3);
	coeff+=c4*pow(eta,3.5);
	coeff+=c5*pow(eta,4);
	coeff+=c6*pow(eta,7.5);
	pws=pc*exp((tc/K) * coeff);
	pw=pws*rhpc*0.01;
	return c*pw/K; /* grams per cubic meter */
}

int main(int argc,char *argv[]) {
	const char *progname;
	int rev=0;
	FILE *in=stdin;
	double rh,degC;
	char line[128];
	
	progname=*argv++; argc--;
	if (argc>0 && argv[0][0]=='-' && argv[0][1]=='r') {
		argv++; argc--;
		rev=1;
	}
	while(argc>=2) {
		int ok=0;
		do {
			char *e,*s;
			s=*(argv++); argc--;   rh=strtod(s,&e); if (e==s) break;
			s=*(argv++); argc--; degC=strtod(s,&e); if (e==s) break;
			if (rev) SWAP(double,rh,degC);
			ok=1;
		} while(0);
		if (ok)
			printf("%f\n",rhtoah(rh,degC));
		else {
			printf("?\n");
			return 0;
		}
		in=NULL;
	}
	if (argc>0) {
		printf("Syntax: %s [-r] [relative_humidity_percent temperature_Celsius]\n",progname);
		return 1;
	}
	if (!in) return 0;
	
	while(fgets(line,sizeof(line),in)) {
		if (sscanf(line,"%lf %lf",&rh,&degC)!=2)
			printf("?\n");
		else {
			if (rev) SWAP(double,rh,degC);
			printf("%f\n",rhtoah(rh,degC));
		}
	}
	fclose(in);
	return 0;
}
