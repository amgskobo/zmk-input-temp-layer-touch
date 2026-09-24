# ZMK Temp Layer Touch Input Processor

[![Test](https://github.com/amgskobo/zmk-input-temp-layer-touch/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-temp-layer-touch/actions/workflows/test.yml)

[English](README.md)

パッドの端から始まった接触が続く間、レイヤーを保持する入力プロセッサです。
キーマップを持つ側で座標からスクロールストリップを判定するので、ドライバ自身がレイヤーを
上げられないパッド（スプリットのペリフェラル側のパッド）でもストリップが使えます。

第一の目的は、driver内蔵のside-scroll判定を再利用可能なinput processorで
置き換えることです。同じslider操作感を保ちながら、2つのinstanceによって
右側と左側のsliderを同時に提供でき、絶対座標がsplit通信を越えた後でも動作します。

repository名と公開processor名は次のように統一しています。

| 項目 | 名前 |
| :--- | :--- |
| West/Zephyr module | `zmk-input-temp-layer-touch` |
| Devicetree compatible | `zmk,input-processor-temp-layer-touch` |
| Kconfig | `ZMK_INPUT_PROCESSOR_TEMP_LAYER_TOUCH` |
| Runtime API prefix | `temp_layer_touch_` |

## 独立したモジュールにしている理由

pad driverはストリップを所有したりkeymap layerを参照したりする必要がありません。splitのperipheralには
keymapがなく、padは`zmk,input-split`でraw inputとしてcentralへ届きます。このprocessorがcentralで
edge routingと選択的なtap抑制を一元的に行います。

このプロセッサは同じこと（接触がどこで始まったか）をセントラルが受け取った座標から判定します。
ドライバは生の入力を送るだけのまま、どの絶対座標パッドでもストリップが使えます。上流に対応する
ものがないので、[abs2rel](https://github.com/amgskobo/zmk-input-abs2rel) や
[padstick](https://github.com/amgskobo/zmk-input-padstick) などと同じく独立したモジュールにしています。

## インストール

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-temp-layer-touch
      remote: amgskobo
      revision: main
```

## 使い方

端の条件ごとにインスタンスを定義し、それを使う全リスナーの**すべてのルートの先頭**に置きます。
端・座標範囲などの devicetree 設定が同じなら、1つの processor node を複数のパッドで共有できます。
接触とボタンの状態は listener index ごとに分離されます。不正なruntime indexは変換せず通過し、
stream 0へaliasしません。

```dts
/ {
    input_processors {
        edge_scroll: edge_scroll {
            compatible = "zmk,input-processor-temp-layer-touch";
            #input-processor-cells = <0>;
            layer = <1>;
            width = <50>;
            edge = "right";
        };
    };
};

&trackpad_listener {
    input-processors = <&edge_scroll>, <&zip_absolute_to_relative>, <&zip_inertia>;

    scroller {
        layers = <1>;
        input-processors = <&edge_scroll>, <&zip_absolute_to_relative_scroll>,
                           <&zip_xy_to_scroll_mapper>;
    };
};
```

ルートはイベントごとに、その時点で有効なレイヤーから選ばれます。このプロセッサが上げるレイヤーで
ルートが変わるので、接触はあるルートで始まって別のルートで終わります。接触が終わる時のルートに
インスタンスがないとリリースを受け取れず、次にパッドに触れるまでレイヤーが残ります。

パッドは絶対座標（`INPUT_ABS_X` / `INPUT_ABS_Y`）と、接触ごとの `INPUT_BTN_TOUCH` を送る必要が
あります。

### driverからの分離と左右slider

driverでは絶対座標reportを有効にします。このprocessorが削除済みのdriver property
`scroll-slider-layer`と`scroll-start`を置き換えます。回転処理はdriverに残し、`edge`は回転適用後に
報告される座標上の辺を指定します。

```dts
&touchpad {
    report-abs;
};

/ {
    input_processors {
        right_scroll_touch: right_scroll_touch {
            compatible = "zmk,input-processor-temp-layer-touch";
            #input-processor-cells = <0>;
            layer = <6>;
            edge = "right";
            width = <50>;
            x-max = <1024>;
            y-max = <1024>;
            trigger-layers = <0>;
        };

        left_scroll_touch: left_scroll_touch {
            compatible = "zmk,input-processor-temp-layer-touch";
            #input-processor-cells = <0>;
            layer = <6>;
            edge = "left";
            width = <50>;
            x-max = <1024>;
            y-max = <1024>;
            trigger-layers = <0>;
        };
    };
};
```

base routeとlayer 6のscroll routeの両方で、左右のinstanceを絶対座標から相対座標への変換より前に
置きます。後段のabsolute-to-relative processorは、たとえば`suppress-btn-touch`によって
`INPUT_BTN_TOUCH`を消費する必要があります。左右は同じlayerを指定できます。共有claimは参照数で
管理されるため、一方のcontactが、もう一方で保持中のlayerを下げることはありません。

```dts
&trackpad_listener {
    input-processors = <&right_scroll_touch>, <&left_scroll_touch>,
                       <&zip_absolute_to_relative>;

    scroller {
        layers = <6>;
        input-processors = <&right_scroll_touch>, <&left_scroll_touch>,
                           <&zip_absolute_to_relative_scroll>,
                           <&zip_xy_to_scroll_mapper>;
    };
};
```

### 設定一覧

| プロパティ | 型 | 既定値 | 説明 |
| :--- | :--- | :--- | :--- |
| `layer` | int | 必須 | 端からの接触の間に保持するレイヤー ID。 |
| `width` | int | 40 | 端からストリップが届く幅（パッドの座標単位）。0 でストリップなし。 |
| `edge` | string | `"right"` | `right` / `left` / `top` / `bottom`。届いた座標での向き（ドライバの回転適用後）。 |
| `x-max` | int | 1024 | パッドが送る X 座標の最大値。 |
| `y-max` | int | 1024 | パッドが送る Y 座標の最大値。 |
| `start-reports` | int | 3 | 接触開始から何レポート目までの位置で端からの接触かを判定するか。 |
| `trigger-layers` | array | 全layer | sliderを開始できる最上位layer ID。別のtemp-layer-touch contactがtargetを保持中なら、そのtargetからの共有開始も許可する。 |
| `pass-buttons` | bool | false | 端からの接触中もパッドのボタンイベントを通す。 |
| `start-disabled` | bool | false | ストリップを無効の状態で起動する。 |

`width` の単位はパッドが報告する絶対座標のcountです。1 countが1 pixelに対応するパッドでだけ
`width = <50>`が50 pixelになります。`x-max`と`y-max`には実際の報告座標の最大値を設定します。

複数の端へ別々のlayerを割り当てる場合は、端ごとにprocessor nodeを定義し、対象となる全routeの
先頭へすべて配置します。角は重なった全stripに含まれるため、両layerを上げる意図がない場合は
corner zoneが重ならない構成にしてください。

## 動作

- **判定に使うのは接触の始まりだけです。** `INPUT_BTN_TOUCH` が押されてから最初の
  `start-reports` レポートの位置をストリップと比べます。パッドの内側で始まって後から端に達した
  ストロークは普通の操作なので、ポインタ操作の途中でストリップが反応することはありません。
  最初のレポートは指の位置より遅れることがあるため、判定期間は1レポートより長くしています。
- **ストリップの判定式**は左右・上下で対称です。右端と下端は
  `max - width` より大きい値、左端と上端は `width` より小さい値で、どの端も幅は `width` です。
- **開始判定windowはdriver型sliderと同じ操作感です。** 既定では、指より遅れて届く最初の座標も
  吸収できるよう、最初の3reportを判定対象にします。
- **開始元layerの制限もdriverと同じです。** `trigger-layers`を省略すると全layerから開始でき、
  指定した場合は現在の最上位layer IDがリスト内にある時だけ開始します。これは削除済みの
  `scroll-slider-trigger-layers`に対応します。唯一の拡張として、別のtemp-layer-touch contactが
  target layerを保持中なら2台目もそのholdへ参加できます。これにより先に離した側が、まだ触れている
  側のlayerを下げません。
- **layerを上げた座標は古いrouteへ流しません。** edge座標が届いたinput thread上でlayerを上げ、
  その座標eventはそこで消費します。次のeventから新しいlayer routeへ入ります。推奨するabs2relでは
  各軸の最初の座標は基準点になるだけなので、この切替で端にpointer stepは発生しません。
- **自分が上げたレイヤーだけを下げます。** 端からの接触が始まった時にすでに有効だったレイヤーは、
  それを上げたものに任せます。
- **同時に始まった端の接触は参照数で管理します。** 複数のリスナーや processor instance が同じ
  レイヤーを要求しても、最後の端接触が終わるまで下げません。接触・ボタン状態が別のリスナーに
  混ざることもありません。
- **レイヤーは接触の終わり（`INPUT_BTN_TOUCH` のリリース）で下げます。** レイヤーを保持したまま
  次のタッチが来た場合も下げます。前の接触のリリースが（スプリット通信の切断などで）失われており、
  ほかに送ってくるものがないためです。
- **端からの接触のタップは捨てます。** ドライバは指を離した後にタップを報告することがあり、
  その時にはレイヤーが下がっていてクリックがポインタのルートに流れます。そのため、端からの接触と
  判定した時点から次の接触が始まるまで `INPUT_BTN_0`〜`INPUT_BTN_15` の押下を捨て、リリースは
  押下を捨てた場合だけ捨てます。`pass-buttons` で無効にできます。
- **消費したeventは何も残しません。** processorのstopが止めるのは、それを返したroute内の
  後続だけです。layer routeではZMKのlistenerがそのeventを自身のhandlerへ渡し、
  `INPUT_BTN_TOUCH`と`INPUT_BTN_0`をマウスボタンとして扱い、syncのたびにreportを送ります。
  そのため消費したeventは、どのhandlerも扱わないcodeに変え、syncも外します。同じchainの
  ほかのprocessorと同じ扱いです。

## 実行時に値を変える

[zmk-feature-custom-settings](https://github.com/cormoran/zmk-feature-custom-settings) と
`CONFIG_ZMK_INPUT_TEMP_LAYER_TOUCH_CUSTOM_SETTINGS=y` で、インスタンスごとに3つの値が
`amgskobo__tlt` サブシステムとして公開され、[DYA Studio](https://studio.dya.cormoran.works/)
などの Studio クライアントで一覧・編集できます。

| キー | 型 | 意味 |
| :--- | :--- | :--- |
| `<node>.enabled` | bool | 端からの接触でレイヤーを保持するかどうか。 |
| `<node>.layer` | layer | 保持するレイヤー。 |
| `<node>.width` | int | ストリップの幅。0 から、端の軸方向のパッドの大きさまで。 |

永続化は設定レジストリが担い、このmoduleは保存しません。更新の前後をatomic generation counterで
囲み、設定変更と重なったinput eventは旧設定と新設定を混ぜて流さず破棄します。破棄したeventがタッチの
押下・リリースだった場合も、接触の開始・終了と前の接触が保持していたレイヤーの解放だけは行うため、
設定変更でレイヤーが上がったまま残ることはありません。保持中のレイヤーは接触が終わるまで
変更前の番号のままです。ストリップを無効にすると、そのinstanceが所有する全layer claimを即座に解放し、
listenerごとのcontact履歴とbutton抑制履歴も消去します。

物理レイアウトのタッチパッドノードからパッドとストリップを紐付けると、クライアントがパッドの横に
この設定を表示します。

```dts
linked-device-identifiers = "abs_rel", "edge_scroll";
linked-subsystems = "amgskobo__a2r", "amgskobo__tlt";
```

## テスト

```sh
bash ./tests/run.sh
bash ./tests/run-integration-docker.sh upstream
bash ./tests/run-integration-docker.sh dya
```

`tests/run.sh` は判定用のヘッダを厳しい警告設定でコンパイルし、最適化ビルドと
AddressSanitizer・UBSan 付きビルドとカバレッジ計測でチェックします。CIは純粋な
判定ヘッダーの行・分岐100%を要求します。Zephyr側driver全体の値ではありません。
結合テストの各 variant は、2つの
input listener が両方の slider node を共有するファームウェアをビルドし、その ZMK 上で
native_sim の自己テストを実行します。確認する内容は、範囲外の listener index を何も変えずに
通すこと、layer を上げた座標と端からの接触のタップを code も sync も残さずに捨てること、
2つの接触が両方終わるまで1つの layer を保持すること、捨てたタップが既定 route でも
layer route でもマウスレポートに届かないことです。`upstream` は ZMK `main`、`dya` は
custom settings を含む DYA 版 ZMK を使います。GitHub Actions は pull request ごとと `main` への
push でこの3系統を実行します。

## ライセンス

MIT License。詳しくは [LICENSE](LICENSE) を参照してください。
