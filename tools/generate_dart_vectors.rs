// Generate synthetic DART LZH blocks with the independent codec Snow uses.
// Run as a retrocompressor 1.0.1 example; see tests/fixtures/dart/README.md.
use retrocompressor::lzss_huff::{compress_slice, Options};
fn main() {
    let directory = std::env::args().nth(1).expect("output directory");
    std::fs::create_dir_all(&directory).unwrap();
    let options = Options { header: false, in_offset: 0, out_offset: 0,
        window_size: 4096, threshold: 2, lookahead: 60, precursor: 0,
        max_file_size: 30000 };
    let mut random: u32 = 0x12345678;
    for name in ["zero", "pattern", "literal"] {
        let data: Vec<u8> = (0..20960).map(|i| {
            match name {
                "zero" => 0,
                "pattern" => if i < 20480 {
                    ((i / 512 * 17 + i % 512 * 13) & 255) as u8
                } else { (((i - 20480) / 12 * 7 + (i - 20480) % 12 + 1) & 255) as u8 },
                _ => { random ^= random << 13; random ^= random >> 17;
                       random ^= random << 5; random as u8 }
            }
        }).collect();
        let compressed = compress_slice(&data, &options).unwrap();
        std::fs::write(format!("{directory}/{name}.lzh"), compressed).unwrap();
    }
}
