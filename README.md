Simple utility that fetches random facts from Wikipedia, saves them locally and displays on your Linux terminal.

<img src="https://github.com/uiriansan/shell-facts/blob/main/prev.png" width="100%" />

# Build:

### 1. Install system dependencies:
- [Python3](https://www.python.org/);
- [sqlite3](https://sqlite.org/);
- [chafa](https://github.com/hpjansson/chafa);
- [glib2](https://docs.gtk.org/glib/);
- [pkg-config](https://www.freedesktop.org/wiki/Software/pkg-config/)

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
python get-facts.py
```
If the download crashes, you can resume from where it stopped by passing `day` and `month` as arguments to get-facts.py:
```bash
python get-facts.py <day> <month>
# e.g.: python get-facts.py 16 5
#                           May 16th
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

- `-r, --raw`:
Outputs raw data separated by '||' (see below);
- `-i, --no-img`:
Do not display the thumbnail;
- `-p, --db-path <path>`:
Changes the path to the database. By default, the program will look for 'facts.db' in the same directory as the executable;
- `-t, --type <type>`:
The type of fact to be displayed. Options are: `selected`, `births`, `deaths`, `events` and `holidays`. Default is 'selected';
- `-d, --day <day>`:
Display a fact for a specific day. Defaults to the current system date;
- `-m, --month <month>`:
Display a fact for a specific month. Defaults to the current system date

> `-r, --raw` outputs data in the following format:</br>
> `text`||`thumbnail`||`thumb_w`||`thumb_h`||`year`||`pages` </br></br>
> `pages` is a stringified json array:
> ```json
> [
>     {
>         "title": "",
>         "thumb": "",
>         "thumb_w": 0,
>         "thumb_h": 0,
>         "url": ""
>     }
> ]
> ```

