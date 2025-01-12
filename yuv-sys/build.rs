use cc;
use rayon::prelude::*;
use regex::Regex;
use std::borrow::Cow;
use std::path::Path;
use std::{env, path::PathBuf};
use std::{fs, io};

const LIBYUV_REPO: &str = "https://github.com/lemenkov/libyuv";
const LIBYUV_COMMIT: &str = "main";
const FNC_PREFIX: &str = "rs_";

fn run_git_cmd(current_dir: &PathBuf, args: &[&str]) -> std::process::ExitStatus {
    println!("cargo:warning=Executing git command: git {}", args.join(" "));
    println!("cargo:warning=Current directory: {:?}", current_dir);
    
    // First verify git is installed and accessible
    let git_version = std::process::Command::new("git")
        .arg("--version")
        .output();
    
    match git_version {
        Ok(output) => {
            println!("cargo:warning=Git version: {}", String::from_utf8_lossy(&output.stdout));
            if !output.status.success() {
                println!("cargo:warning=Git version check failed. Stderr: {}", 
                    String::from_utf8_lossy(&output.stderr));
                panic!("Git is not properly installed or not in PATH");
            }
        },
        Err(e) => {
            panic!("Failed to execute git --version: {}. Is git installed and in PATH?", e);
        }
    }

    let output = std::process::Command::new("git")
        .current_dir(current_dir)
        .args(args)
        .output()
        .unwrap_or_else(|e| {
            panic!("Failed to execute git command: {}. Is git installed?", e);
        });
    
    if !output.status.success() {
        println!("cargo:warning=Git command failed: git {}", args.join(" "));
        println!("cargo:warning=Working directory: {:?}", current_dir);
        println!("cargo:warning=Stdout: {}", String::from_utf8_lossy(&output.stdout));
        println!("cargo:warning=Stderr: {}", String::from_utf8_lossy(&output.stderr));
        panic!("Git command failed: git {}", args.join(" "));
    }
    
    output.status
}

fn rename_symbols(
    fnc_list: &[&str],
    include_files: &[fs::DirEntry],
    source_files: &[fs::DirEntry],
) {
    include_files.par_iter().chain(source_files).for_each(|file| {
        let mut content = fs::read_to_string(&file.path()).unwrap();
        for line in fnc_list {
            let fnc = line.trim();
            if fnc.is_empty() {
                continue;
            }

            let split: Vec<&str> = fnc.split_whitespace().collect();
            let fnc = split[0];
            let new_name = if split.len() > 1 {
                split[1].to_owned()
            } else {
                format!("{}{}", FNC_PREFIX, fnc)
            };

            let re = Regex::new(&format!(r"\b{}\b", fnc)).unwrap();
            if let Cow::Owned(c) = re.replace_all(&content, &new_name) {
                content = c
            }
        }

        fs::write(&file.path(), content.to_string()).unwrap();
    });
}

fn copy_dir(source: impl AsRef<Path>, destination: impl AsRef<Path>) -> io::Result<()> {
    fs::create_dir_all(&destination)?;
    for entry in fs::read_dir(source)? {
        let entry = entry?;
        if entry.file_type()?.is_dir() {
            copy_dir(entry.path(), destination.as_ref().join(entry.file_name()))?;
        } else {
            fs::copy(entry.path(), destination.as_ref().join(entry.file_name()))?;
        }
    }
    Ok(())
}

fn clone_if_needed(output_dir: &PathBuf, libyuv_dir: &PathBuf) -> bool {
    if libyuv_dir.exists() {
        println!("cargo:warning=libyuv directory already exists at {:?}", libyuv_dir);
        return false;
    }

    println!("cargo:warning=Cloning libyuv from {} into {:?}", LIBYUV_REPO, libyuv_dir);
    
    // Create parent directory if it doesn't exist
    fs::create_dir_all(output_dir).unwrap_or_else(|e| {
        panic!("Failed to create output directory {:?}: {}", output_dir, e);
    });

    // Clone repository with verbose output
    let status = run_git_cmd(output_dir, &["clone", "--verbose", LIBYUV_REPO]);
    if !status.success() {
        println!("cargo:warning=Failed to clone libyuv repository");
        println!("cargo:warning=Current directory: {:?}", output_dir);
        println!("cargo:warning=Git version:");
        let _ = std::process::Command::new("git").arg("--version").status();
        if libyuv_dir.exists() {
            fs::remove_dir_all(&libyuv_dir).unwrap();
        }
        panic!("failed to clone libyuv, is git installed and in PATH?");
    }

    // Verify the repository was cloned
    if !libyuv_dir.exists() {
        println!("cargo:warning=Cloned repository directory does not exist at {:?}", libyuv_dir);
        println!("cargo:warning=Output directory contents:");
        if let Ok(entries) = fs::read_dir(output_dir) {
            for entry in entries {
                if let Ok(entry) = entry {
                    println!("cargo:warning=  {:?}", entry.path());
                }
            }
        }
        panic!("Repository clone failed - no directory created");
    }

    println!("cargo:warning=Checking out libyuv commit {}", LIBYUV_COMMIT);
    let status = run_git_cmd(&libyuv_dir, &["checkout", LIBYUV_COMMIT]);
    if !status.success() {
        println!("cargo:warning=Failed to checkout commit {}", LIBYUV_COMMIT);
        println!("cargo:warning=Available branches:");
        let _ = std::process::Command::new("git")
            .current_dir(&libyuv_dir)
            .arg("branch")
            .arg("-a")
            .status();
        fs::remove_dir_all(&libyuv_dir).unwrap();
        panic!("failed to checkout to {}", LIBYUV_COMMIT);
    }

    // Verify the cloned repository structure
    println!("cargo:warning=Verifying cloned repository structure at {:?}", libyuv_dir);
    let required_dirs = ["include", "source"];
    for dir in required_dirs {
        let dir_path = libyuv_dir.join(dir);
        if !dir_path.exists() {
            println!("cargo:warning=Cloned repository is missing required directory: {}", dir);
            println!("cargo:warning=Directory contents of {:?}:", libyuv_dir);
            if let Ok(entries) = fs::read_dir(&libyuv_dir) {
                for entry in entries {
                    if let Ok(entry) = entry {
                        println!("cargo:warning=  {:?}", entry.path());
                    }
                }
            }
            panic!("Cloned repository is missing required directory: {}", dir);
        }
    }

    println!("cargo:warning=Successfully cloned and verified libyuv repository");
    true
}

fn main() {
    let output_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let libyuv_dir = output_dir.join("libyuv");
    let include_dir = libyuv_dir.join("include");
    let source_dir = libyuv_dir.join("source");

    let cloned = clone_if_needed(&output_dir, &libyuv_dir);

    println!("cargo:warning=OUT_DIR: {:?}", output_dir);
    println!("cargo:warning=libyuv_dir: {:?}", libyuv_dir);
    println!("cargo:warning=include_dir: {:?}", include_dir);
    println!("cargo:warning=source_dir: {:?}", source_dir);

    // Verify libyuv directory structure
    if !libyuv_dir.exists() {
        panic!("libyuv directory does not exist at: {:?}", libyuv_dir);
    }
    
    println!("cargo:warning=Looking for include files at: {:?}", include_dir);
    if !include_dir.exists() {
        panic!("libyuv include directory does not exist at: {:?}", include_dir);
    }

    let include_files = fs::read_dir(include_dir.join("libyuv"))
        .unwrap_or_else(|e| {
            println!("cargo:warning=Failed to read include directory {:?}: {}", include_dir.join("libyuv"), e);
            println!("cargo:warning=Directory contents of {:?}:", include_dir);
            if let Ok(entries) = fs::read_dir(&include_dir) {
                for entry in entries {
                    if let Ok(entry) = entry {
                        println!("cargo:warning=  {:?}", entry.path());
                    }
                }
            }
            panic!("Failed to read include directory {:?}: {}", include_dir.join("libyuv"), e);
        })
        .map(Result::unwrap)
        .filter(|f| f.path().extension().unwrap() == "h")
        .collect::<Vec<_>>();

    println!("cargo:warning=Looking for source files at: {:?}", source_dir);
    if !source_dir.exists() {
        panic!("libyuv source directory does not exist at: {:?}", source_dir);
    }

    let source_files = fs::read_dir(&source_dir)
        .unwrap_or_else(|e| {
            panic!("Failed to read source directory {:?}: {}", source_dir, e);
        })
        .map(Result::unwrap)
        .filter(|f| f.path().extension().unwrap() == "cc")
        .collect::<Vec<_>>();

    println!("cargo:warning=Current working directory: {:?}", env::current_dir().unwrap());
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    println!("cargo:warning=CARGO_MANIFEST_DIR: {:?}", manifest_dir);
    
    // Check if yuv_functions.txt exists in the correct location
    let fnc_path = PathBuf::from(&manifest_dir).join("yuv_functions.txt");
    println!("cargo:warning=Looking for yuv_functions.txt at: {:?}", fnc_path);
    
    // Verify the file exists and is readable
    if !fnc_path.exists() {
        println!("cargo:warning=Directory contents of {:?}:", manifest_dir);
        if let Ok(entries) = fs::read_dir(&manifest_dir) {
            for entry in entries {
                if let Ok(entry) = entry {
                    println!("cargo:warning=  {:?}", entry.path());
                }
            }
        }
        panic!("yuv_functions.txt does not exist at: {:?}", fnc_path);
    }

    println!("cargo:warning=Found yuv_functions.txt at {:?}", fnc_path);
    println!("cargo:warning=File permissions: {:?}", fs::metadata(&fnc_path).unwrap().permissions());
    
    let fnc_content = fs::read_to_string(&fnc_path).unwrap_or_else(|e| {
        println!("cargo:warning=Detailed error info - Path: {:?}, Error: {:?}", fnc_path, e);
        println!("cargo:warning=Directory contents:");
        if let Ok(entries) = fs::read_dir(&manifest_dir) {
            for entry in entries {
                if let Ok(entry) = entry {
                    println!("cargo:warning=  {:?}", entry.path());
                }
            }
        }
        panic!("Failed to read yuv_functions.txt: {}. Path: {:?}", e, fnc_path);
    });
    let fnc_list = fnc_content.lines().collect::<Vec<_>>();

    if cloned {
        rename_symbols(&fnc_list, &include_files, &source_files);
    }

    cc::Build::new()
        .warnings(false)
        .include(libyuv_dir.join("include"))
        .files(source_files.iter().map(|f| f.path()))
        .compile("yuv");

    let mut bindgen = bindgen::Builder::default()
        .header(include_dir.join("libyuv.h").to_string_lossy())
        .clang_arg(format!("-I{}", include_dir.to_str().unwrap()));

    for fnc in fnc_list {
        let new_name = format!("{}{}", FNC_PREFIX, fnc);
        bindgen = bindgen.allowlist_function(&new_name);
    }

    let output = bindgen.generate().unwrap();
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap()).join("yuv.rs");
    output.write_to_file(out_path).unwrap();

    println!("cargo:rerun-if-changed=yuv_functions.txt");
}
