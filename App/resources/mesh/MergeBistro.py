import os
import glob

def join_files(input_dir, output_file_path, file_pattern):
    """
    分割されたファイルを結合して単一のファイルに戻す関数。

    Args:
        input_dir (str): 分割ファイルが保存されているディレクトリ。
        output_file_path (str): 結合後の出力ファイルパス。
        file_pattern (str): 結合するファイルの検索パターン (例: "myarchive.zip.part*")。
    """
    search_path = os.path.join(input_dir, file_pattern)
    # パターンに一致するファイルリストを取得し、名前順にソート
    part_files = sorted(glob.glob(search_path))

    if not part_files:
        print(f"Error: No files found matching the pattern '{search_path}'")
        return

    print("Found the following parts to join:")
    for part in part_files:
        print(f"  - {os.path.basename(part)}")

    try:
        with open(output_file_path, 'wb') as f_out:
            for part_file in part_files:
                print(f"Appending {os.path.basename(part_file)}...")
                with open(part_file, 'rb') as f_in:
                    # 分割ファイルの内容をすべて読み込み、出力ファイルに書き込む
                    f_out.write(f_in.read())
        
        print(f"\n✅ File joining completed successfully. Output: {output_file_path}")

    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == '__main__':
    print("\n" + "="*10 + " STARTING FILE MERGE " + "="*10)
    JOINED_FILE = "Bistro.zip" # 結合後のファイル名
    # 分割時に作られたファイル名のパターンを指定
    file_pattern = f"{JOINED_FILE}.part*"

    join_files(
        input_dir=".",
        output_file_path=JOINED_FILE,
        file_pattern=file_pattern
    )
    print("\n" + "="*10 + " END FILE MERGE " + "="*10)
