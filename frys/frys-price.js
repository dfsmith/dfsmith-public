/* Scrape the price of a PLU from frys.com. */
/* See http://casperjs.org for description of CasperJS. */
/*    apt install npm nodejs-legacy
 *    npm install casperjs
 *    npm install phantomjs
 *    export PHANTOMJS_EXECUTABLE="$PWD/node_modules/phantomjs/bin/phantomjs"
 *    ./node_modules/casperjs/bin/casperjs frys-price.js 7550776
 * or ./node_modules/casperjs/bin/casperjs --verbose --log-level=debug frys-price.js 7550776
 */

var casper=require('casper').create({willNavigate: false});

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

casper.start(frys_link(casper.cli.get(0)));

casper.then(function() {
	var price,title;
	price=frys_price();
	title=frys_title();
	//this.echo(this.getTitle()+" "+price);
	this.echo(price+"\t"+title);
});

casper.run();
