// build.rs

fn main() {
    cc::Build::new()
        .flag("-v")
        .include("include")
        .file("src/test.c")
        .file("src/utils.c")
        .file("src/lexer.c")
        .file("src/parser.c")
        .file("src/runtime.c")
        .file("src/library.c")
        .compile("pit");
}
