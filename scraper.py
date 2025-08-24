import requests, time, sqlite3, json, sys
from calendar import monthrange
from pathlib import Path

WIKIPEDIA_ENDPOINT = "https://api.wikimedia.org/feed/v1/wikipedia/en/onthisday/all"
DB_PATH = "./facts.db"
MAX_ATTEMPTS = 10
REQUEST_DELAY = 5 # secods to wait before each request
FAIL_DELAY = 10 # seconds to wait if a request fails before trying again

attempts = 0

def get_facts_from_day(i_day, i_month, con, cur):
    for month in range(i_month, 13):
        ( _, days ) = monthrange(2012, month)
        for day in range(i_day, days + 1):
            response = requests.get(f"{WIKIPEDIA_ENDPOINT}/{month:02}/{day:02}")
            if response.status_code == 200:
                data = response.json()
                for key, facts in data.items():
                    for i in range(len(facts)):
                        text = facts[i].get("text")
                        year = facts[i].get("year") or "NULL"
                        pages_raw = facts[i].get("pages")
                        pages = [
                            {
                                "title": page.get("title"),
                                "thumb": page.get("thumbnail").get("source") if page.get("thumbnail") else "",
                                "url": page.get("content_urls").get("desktop").get("page") if page.get("content_urls") and page.get("content_urls").get("desktop") else ""
                            }
                            for page in pages_raw
                        ]
                        facts[i] = (text, key, day, month, year, json.dumps(pages))

                facts = data["selected"] + data["births"] + data["deaths"] + data["events"] + data["holidays"]
                print(f"\033[32m[{day:02}/{month:02} OK] ->\033[37m Fetched {len(facts)} facts.")

                cur.executemany("INSERT INTO Facts VALUES (?, ?, ?, ?, ?, ?)", facts)
                con.commit()
            else:
                print(f"\033[31m[{day:02}/{month:02} ERROR] ->\033[37m Retrying in {FAIL_DELAY}s...")
                if attempts < MAX_ATTEMPTS:
                    time.sleep(FAIL_DELAY)
                    return get_facts_from_day(day, month, con, cur)

            # Reset day so it wraps to the first day of the next month correctly in case we specify a initial date
            i_day = 1
            time.sleep(REQUEST_DELAY)

if __name__ == "__main__":
    i_day = 1 # 1st
    i_month = 1 # January

    # Use `python scraper.py <day> <month>` to start fetching from a specific date
    if len(sys.argv) > 1:
        if d := sys.argv[1]:
            i_day = int(d) if int(d) >= 1 and int(d) <= 31 else 1
        if m := sys.argv[2]:
            i_month = int(m) if int(m) >= 1 and int(m) <= 12 else 1

    resolved_db_path = Path(DB_PATH).resolve()

    # Delete DB if we're not resuming the process
    if i_day == 1 and i_month == 1 and resolved_db_path.is_file():
        try:
            resolved_db_path.unlink()
        except Exception as e:
            print(f"Error: {e}")

    con = sqlite3.connect(resolved_db_path)
    cur = con.cursor()

    # Create DB
    cur.execute("""
        CREATE TABLE IF NOT EXISTS Facts(
            text TEXT NOT NULL,
            type TEXT NOT NULL,
            day INT NOT NULL, 
            month INT NOT NULL, 
            year INT, 
            pages TEXT
        )
    """)

    get_facts_from_day(i_day, i_month, con, cur)

    con.close()
