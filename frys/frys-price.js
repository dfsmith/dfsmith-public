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
casper.options.waitTimeout=20000;

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
	tt=casper.getHTML('h3.product-title strong');
	log("Title: "+tt);
	if (!tt) {
		tt=casper.getHTML('#ProductName .product_title');
		log("Title (nobold): "+tt);
	}
	return tt;
}

function frys_price() {
	//var pp=casper.getHTML('div#product_price_holder .productPrice label');
	var pp=casper.getHTML('span#did_price1valuediv');
	log("Price: "+pp);
	return pp;
}

function frys_available() {
	var ss,aa,big;

	big=casper.evaluate(function() {
		return $('div#nbstores td.storeTD').first().text();
	});
	ss=big.replace(/ *\([^)]*\) */g,"").trim();
	log("Store: "+ss);

	big=casper.evaluate(function() {
		return $('div#nbstores td.sStatusTD').first().text();
	});
	aa=big.trim();
	log("Stock: "+aa);

	return ss+": "+aa;
}

casper.start(frys_link(plu));

casper.then(function() {
	price=frys_price();
	title=frys_title();	
});

if (zip) {
	casper.then(function() {
		this.fillSelectors('#changeStore form', {
			'input[id="zcode"]': zip,
			'input[id="zplu"]' : plu,
		});
		this.click('#changeStore form #zbtn');
	});

	casper.waitForSelector('div.sub_nbstores');

	casper.then(function() {
		//log(casper.evaluate(function() {return document.all[0].outerHTML}));
		store=frys_available();
	});
}

casper.then(function() {
	this.echo(price+"\t"+title);
	if (store) {
		this.echo("\t"+store);
	}
});

casper.run();
