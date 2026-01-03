const GIFT: [&str; 12] = [
    "partridge in a pair tree.",
    "turtle doves,",
    "French hens,",
    "calling birds,",
    "golden rings,",
    "geese a-laying,",
    "swans a-swimming,",
    "maids a-milking,",
    "ladies dancing,",
    "lords a-leaping,",
    "pipers piping,",
    "drummers drumming,",
];

const ORDINAL: [&str; 12] = [
    "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "tenth",
    "eleventh", "twelfth",
];

const NUMBER: [&str; 12] = [
    "a", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
];

fn main() {
    for day in 0..12 {
        println!(
            "On the {} day of Christmas, my true love sent to me:",
            ORDINAL[day]
        );
        for gift in (0..=day).rev() {
            if day != 0 && gift == 0 {
                print!("and ");
            }
            println!("{} {}", NUMBER[gift], GIFT[gift]);
        }
        println!();
    }
}
