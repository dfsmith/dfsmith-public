/* Scrape the price of a PLU from frys.com. */
/* See http://casperjs.org for description of CasperJS. */
/*    apt install npm nodejs-legacy
 *    npm install casperjs
 *    npm install phantomjs
 *    export PHANTOMJS_EXECUTABLE="$PWD/node_modules/phantomjs/bin/phantomjs"
 *    ./node_modules/casperjs/bin/casperjs frys-price.js 7550776
 * or ./node_modules/casperjs/bin/casperjs --verbose --log-level=debug frys-price.js 7550776
 */

var casper=require('casper').create();

var plu,zip;
plu=casper.cli.get(0);
zip=casper.cli.get(1);

var price,title,store;

function log(msg) {
	casper.log(msg,'debug');
}

function frys_link(plu) {
	return "http://frys.com/product/"+plu;
}

function frys_title() {
	var tt;
	tt=casper.getHTML('#ProductName .product_title b');
	log(tt);
	if (tt.empty) {
		tt=casper.getHTML('#ProductName .product_title');
	}
	return tt;
}

function frys_price() {
	var pp=casper.getHTML('div#product_price_holder .productPrice label');
	log(pp);
	return pp;
}

function frys_available() {
	var ss,aa
	ss=casper.getHTML('div#nbstores td.storeTD');
	log(ss);
	aa=casper.getHTML('div#nbstores td.sStatusTD');
	log(aa);
	return ss+":"+aa;
}

casper.start(frys_link(plu));

casper.then(function() {
	price=frys_price();
	title=frys_title();	
});

casper.then(function() {
	if (zip.empty) {return;}
	this.fill('#changeStore form', {
		'zcode': zip,
		'zplu' : plu,
	},true);
	store=frys_available();
});

casper.then(function() {
	//this.echo(this.getTitle()+" "+price);
	log(price+title);
	this.echo(price+"\t"+title);
	if (!store.empty) {
		this.echo("\t"+store);
	}
});

casper.run();
