fn f2c(f: f64) -> f64 {
    (f - 32.0) * 5.0 / 9.0
}

fn c2f(c: f64) -> f64 {
    (c * 9.0 / 5.0) + 32.0
}

fn main() {
    let dir = std::env::args().nth(1).unwrap_or_else(|| ".".to_string());
    let val: f64 = std::env::args()
        .nth(2)
        .expect("Please provide a temperature value")
        .parse()
        .expect("Need a number");
    let from: &str;
    let to: &str;
    let result = match dir.as_str() {
        "-c" => {
            from = "Celsius";
            to = "Fahrenheit";
            c2f(val)
        }
        "-f" => {
            from = "Fahrenheit";
            to = "Censius";
            f2c(val)
        }
        &_ => panic!("Unknown direction: use -c or -f"),
    };
    println!("{val} {from} is {result} {to}.");
}
