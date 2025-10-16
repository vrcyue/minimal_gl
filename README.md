# MinimalGL


# 解説

PC Intro 作成ツールです。
GLSL に対応しています。

PC Intro とは、windows PC 上で動作する数キロバイト程度の小さな実行ファイルで、音と映像を生成し、その内容で競うメガデモ（デモシーン）のカテゴリです。  
PC Intro の実行ファイルは単体で動作することが要求されます。
外部ファイルやネットワーク上のデータを参照することは禁じられています（例外として OS に標準装備されている dll などの利用は許されています）。  
音と映像いずれも実行時に何らかのアルゴリズムにより生成する必要があります。
そしていかにファイルサイズを小さくするかが重要です。  

PC Intro には、
ファイルサイズ 1024 バイト未満の PC 1K Intro、
4096 バイト未満の PC 4K Intro、
8192 バイト未満の PC 8K Intro、
65536 バイト未満の PC 64K Intro
などのサブカテゴリがあります。  
MinimalGL は、このうち PC 4K Intro の作成に特化したツールです。


# スクリーンショット
作成したシェーダを実行ファイルとしてエクスポートしている様子。  
![screen_shot_gfx](https://user-images.githubusercontent.com/11882108/82467562-c1ad0800-9afc-11ea-8582-5ef5dbdf2e0f.png)

シェーダで生成したサウンドを、波形を可視化しながら再生している様子。
![screen_shot_snd](https://user-images.githubusercontent.com/11882108/82468262-8fe87100-9afd-11ea-8f94-ebf531cb53be.png)

ユーザーテクスチャを読み込み、skybox として利用している様子。  
※ユーザーテクスチャはエクスポートされた実行ファイルからは利用できません。
![screen_shot_user_texture](https://user-images.githubusercontent.com/11882108/83782228-7c770180-a6ca-11ea-8ab6-a051cbe10515.png)


# 準備

MinimalGL を利用するには、以下のツールが必要です。

- Microsoft Visual Studio  
	2019 以降を推奨。

- shader_minifier.exe と crinkler.exe  
	https://github.com/laurentlb/Shader_Minifier/releases  
	http://www.crinkler.net/  
	minimal_gl.exe と同じディレクトリ、もしくはパスの通ったディレクトリにコピーしておきます
	（minimal_gl.exe と同じディレクトリに置かれた方が優先されます）。


# 使い方

MinimalGL を使った PC 4K Intro 作成の簡単な流れは以下のようになります。

1. シェーダファイルを用意する  
	example/ 以下の適当な *.gfx.glsl *.snd.glsl ファイルからコピーしてください。

2. グラフィクスシェーダファイルを開く  
	メインウィンドウに \*.gfx.glsl ファイルをドラッグ＆ドロップするか、
	メニューから [File]→[Open Graphics Shader] を選択し \*.gfx.glsl ファイルを読み込みます。

3. サウンドシェーダファイルを開く  
	メインウィンドウに \*.snd.glsl ファイルをドラッグ＆ドロップするか、
	メニューから [File]→[Open Sound Shader] を選択し \*.snd.glsl ファイルを読み込みます。

4. シェーダのエディット  
	MinimalGL はシェーダのエディット機能を持ちません。
	シェーダのエディットはユーザーお手持ちのテキストエディタ等でシェーダファイルを直接書き換えることで行います。  
	現在描画中（再生中）の glsl ファイルのタイムスタンプが更新されると、
	直ちにリコンパイルされ描画結果（再生結果）に反映されます。

5. 実行ファイルにエクスポート  
	メニューから [File]→[Export Executable] を選択します。
	Output file に生成する実行ファイル名を指定し OK をクリックします。
	エクスポートに成功するとファイルサイズがメッセージボックスで表示されます。

6. minify  
	ファイルサイズが 4096 バイト未満になっていない場合、ファイルサイズ削減作業（minify）を行います。  
	実行ファイルエクスポート時に同時に生成される minify されたシェーダコード（\*.inl ファイル）や
	圧縮結果のレポート（\*.crinkler_report.html）等を参考に、シェーダコードを短くしていきます。  
	実行ファイルエクスポート時の設定を調整したり、
	描画設定（[Setup]→[Render Settings]）から無駄な機能を off にすることでもファイルサイズを削減できます。

7. 完成  
	念のために nVidia AMD Intel 各社の GPU 環境で動作テストしてください。

# パイプラインサンプルの適用手順

`File → Pipeline Management...` から専用ダイアログを開くことで、プロジェクトにカスタムパイプラインを適用できます。

1. **ダイアログを開く**  
	メインメニューの `File → Pipeline Management...` を選択します。現在のパイプライン状態とリソース／パスの一覧が表示されます。

2. **サンプルパイプラインの読み込み**  
	ダイアログ内の `Apply sample` を押すか、`Load...` から `examples/pipeline_sample.json` を選択すると、`scene → compute → composite → present` のサンプル構成がそのまま適用されます。読み込みに成功するとステータス欄に反映されます。

3. **必要に応じて JSON を保存**  
	現在のカスタムパイプラインを JSON として保存する場合は `Save...` を使用します。プロジェクトを通常どおりエクスポートするとパイプラインも一緒に保存されるため、手動保存は任意です。

4. **シェーダを差し替え**  
	サンプルパイプラインに合わせて、グラフィクス用には `examples/shaders/pipeline_demo/pipeline_graphics.glsl`、コンピュート用には `examples/shaders/pipeline_demo/pipeline_compute.glsl` を読み込みます。シェーダ内で `g_pipelinePassIndex` を参照している場合はパス 0/1/2 で動作を切り替えられます。

5. **エクスポート**  
	準備が整った状態で通常どおりエクスポートすると、`pipeline_description.inl` がサンプル構成で生成され、ランタイム実行ファイルでも同じパイプラインが適用されます。

# 機能一覧

# 機能一覧

- シェーダよるグラフィクス描画  
	画面全体に一枚の四角形ポリゴンを表示し、フラグメントシェーダにより描画を行います。

- シェーダよるサウンド生成  
	コンピュートシェーダによるサウンド生成を行います。

- シェーダホットリロード  
	シェーダファイルが更新されると直ちに自動リロードを行います。  
	ライブコーディング用途を想定した、経過時間をリセットせずにリロードするモードも利用可能です（メニューから [Setup]→[Preference Settings] を選択）。

- 実行ファイルエクスポート  
	現在のグラフィクス及びサウンドの内容を実行ファイルにエクスポートします。  
	shader_minifier (https://github.com/laurentlb/Shader_Minifier) によるシェーダコード minify、
	および crinkler (http://www.crinkler.net/) による実行ファイル圧縮が適用されます。

- プロジェクトファイルエクスポート/インポート  
	現在の状態（現在のシェーダファイル名、カメラの位置、描画設定、エクスポート設定等々）をプロジェクトファイルにエクスポートします。
	プロジェクトファイルをインポートすることで状態を復元できます。

- スクリーンショットキャプチャ  
	Unorm8 RGBA フォーマットの png ファイルとしてスクリーンショットをキャプチャします。

- カメラコントロール  
	マウスによるカメラ操作機能が利用可能です（利用するかはオプショナル）。

- キューブマップキャプチャ  
	カメラコントロールを利用している場合、
	現在のカメラ位置から見える全方位の状態をキューブマップとして FP32 RGBA フォーマットの dds ファイルにキャプチャできます。

- サウンドキャプチャ  
	サウンド生成結果を float 2ch 形式の wav ファイルに保存します。

- 連番画像保存  
	グラフィクス生成結果を Unorm8 RGBA フォーマットの連番 png ファイルとして保存します。

- ユーザーテクスチャ  
	任意の画像ファイル（現状 png と dds のみ対応）をテクスチャとして利用できます。
	テクスチャは最大 4 つまで登録可能です。なおこの機能はエクスポートされた exe 上では利用できません。

- 一時停止、スロー再生/スロー巻き戻し、早送り/巻き戻し  


# グラフィクス周り機能一覧

- バックバッファテクスチャの利用  
	前回フレームの描画結果をテクスチャとして利用できます。

- マルチプルレンダーターゲット (MRT)  
	MRT4 まで対応します。バックバッファテクスチャも MRT 枚数分利用できます。

- ミップマップ生成  
	バックバッファテクスチャをミップマップ化します。

- LDR/HDR レンダリング  
	LDR (Unorm8 RGBA) および HDR (FP16 FP32 RGBA) でのレンダリングに対応します。

- コンピュートシェーダ  
	グラフィクスパイプラインに Compute Shader を統合しています。binding=0〜3 は従来通り MRT/バックバッファ、binding=4〜7 は Compute Shader の書き込み結果、binding>=8 はユーザーテクスチャとして扱われます。Compute Shader で生成した結果は次フレームのフラグメントシェーダから sampler2D で参照できます。


# twigl のシェーダの実行ファイル化

https://twigl.app/ の geeker_300_es もしくは geeker_MRT で作成したシェーダは、MinimalGL 上に移植することで、実行ファイルにエクスポートできます。  
詳細は examples 以下の、twigl 互換サンプルを参照してください。  


# トラブルシューティング

- twigl と動作が異なる  
	以下の項目に該当しないか確認してください。

	- uniform float s を利用している  
		twigl.app の uniform float s はサウンド再生位置付近のサンプルの平均ですが、
		MinialGL の twigl 互換サンプルでは（シェーダ上でオンザフライで実行する都合上、処理負荷の問題から）平均を取っていません。
		そのため uniform float s に依存したエフェクトの動作は互換になりません。

	- 未定義の動作が含まれる  
		pow 関数の第一引数が負になっているコード等。

	- 未初期化変数が存在する  


- Shader Minifier に失敗する  
	Shader Minifier には以下の制限事項があります。

	- 三項演算子内でカンマが利用できない  
		以下のコードはパースに失敗します。
		```
		i==0?a=0.,b=0.:0;  
		```
		以下のように修正する必要があります。
		```
		if(i==0)a=0.,b=0.;  
		```

	- ベクトル型に 0 を掛けてはいけない  
		```
		vec3 a = vec3(1.) * .0;  
		```
		このようなコードは、  
		```
		vec3 a =.0;  
		```
		のように変換され、float->vec3 変換によりシェーダコンパイルエラーになります。

	- 配列の要素数の指定方法の制限  
		以下のコードはパースに失敗します。
		```
		float[4] a = float[](0.0, 1.0, 2.0, 3.0);
		```
		以下のように修正する必要があります。
		```
		float a[4] = float[](0.0, 1.0, 2.0, 3.0);
		```


# 制限事項

MinimalGL には以下の制限事項があります。

- windows exe 専用です。

- バーテクスシェーダは利用できません。

- サウンドはコンピュートシェーダによる生成のみに対応します。


# ライセンス

- 外部由来のもの  
	- src/external/cJSON 以下  
		cJSON のライセンスに従います。  
		入手元 : https://github.com/DaveGamble/cJSON

	- src/external/imgui 以下  
		imgui のライセンスに従います。  
		入手元 : https://github.com/ocornut/imgui

	- src/external/stb 以下  
		stb のライセンスに従います。  
		入手元 : https://github.com/nothings/stb

	- src/GL/ 及び src/KHR/ 以下  
		ソースコード中に書かれているライセンスに従います。  
		入手元 : https://github.com/KhronosGroup/OpenGL-Registry  
		入手元 : https://github.com/skaslev/gl3w

	- examples/ 以下  
		twigl 互換サンプルコードに含まれる main 関数は twigl のデフォルトシェーダを流用しています。
		twigl のライセンスに従います。  
		入手元 : https://github.com/doxas/twigl


- 上記以外  
	(C) Yosshin (aka 0x4015)  
	Apache License Version 2.0 が適用されます。  
	This software includes the work that is distributed in the Apache License 2.0


# 謝辞 Special Thanks

- Mentor/TBC and Blueberry/Loonies  
	The creators of Crinkler, a great compression tool.


- LLB/Ctrl-Alt-Test  
	The creator of Shader Minifier, a great minify tool.

