fn fib_loop(n: usize) -> usize {
    let mut a = 0;
    let mut b = 1;
    for _ in 0..n {
        let temp = a;
        a = b;
        b = temp + b;
    }
    a
}

fn main() {
    let n: usize = std::env::args()
        .nth(1)
        .expect("Please provide a Fibonacci index")
        .parse()
        .expect("Need a number");
    for f in 0..n {
        println!("Fibonacci({}) = {}", f, fib_loop(f));
    }
}
