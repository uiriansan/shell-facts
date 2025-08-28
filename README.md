Simple utility that fetches random facts from Wikipedia, saves them locally and displays on your terminal.

# Build:

### 1. Install system dependencies:
- [Python3](https://www.python.org/);
- [sqlite3](https://sqlite.org/);
- [chafa](https://github.com/hpjansson/chafa);

### 2. Clone this repo:
```bash
git clone --recursive https://github.com/uiriansan/shell-facts.git
cd shell-facts/
```

### 3. Install Python dependencies:
```bash
pip install -r requirements.txt
```
If you're not familiar with Python, simply create a virtual environment and source it:
```bash
python -m venv venv
source venv/bin/activate
# or if you use Fish:
    source venv/bin/activate.fish

pip install -r requirements.txt
```

### 4. Download data from Wikipedia:
```bash
python scraper.py
```
If the download crashes, you can resume from where it stopped by passing `day` and `month` as arguments to scraper.py:
```bash
python scraper.py <day> <month>
# e.g.: python scraper.py 16 5
#                         May 16th
```

### 5. Build the C program:
```bash
make && make run
```

### 6. Run:
```bash
./shell-facts
```


# Usage:
You can tweak the output of `shell-facts` with the following options:

- `-h, --help`
Prints help.
- `-r, --raw`
Outputs raw data separated by '||'
-`-d, --db-path <path>`
Changes the path to the databse. By default, the program will look for 'facts.db' in the same directory as the executable.
-`-t, --type <type>`
The type of fact to be displayed. Options are: `selected`, `births`, `deaths`, `events` and `holidays`. Default is 'selected'.

