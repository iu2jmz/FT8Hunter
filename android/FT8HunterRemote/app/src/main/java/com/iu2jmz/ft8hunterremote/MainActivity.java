package com.iu2jmz.ft8hunterremote;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.Space;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.net.URI;

public final class MainActivity extends Activity {
    private static final String PREFS = "ft8_hunter_remote";
    private static final String KEY_URL = "server_url";
    private static final String KEY_TOKEN = "token";

    private static final int BG = Color.rgb(6, 16, 28);
    private static final int PANEL = Color.rgb(11, 27, 43);
    private static final int CYAN = Color.rgb(22, 215, 255);
    private static final int TEXT = Color.rgb(231, 245, 255);
    private static final int MUTED = Color.rgb(130, 169, 196);
    private static final int RED = Color.rgb(255, 83, 100);

    private SharedPreferences prefs;
    private WebView webView;
    private String serverUrl = "";
    private String token = "";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().setStatusBarColor(BG);
        getWindow().setNavigationBarColor(BG);
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);

        serverUrl = prefs.getString(KEY_URL, "");
        token = prefs.getString(KEY_TOKEN, "");

        if (serverUrl.isEmpty() || token.isEmpty()) {
            showSetup();
        } else {
            showRemote();
        }
    }

    private int dp(int value) {
        float density = getResources().getDisplayMetrics().density;
        return Math.round(value * density);
    }

    private GradientDrawable rounded(int color, int strokeColor, int radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        if (strokeColor != Color.TRANSPARENT) {
            d.setStroke(dp(1), strokeColor);
        }
        return d;
    }

    private TextView label(String text, int sizeSp, int color, boolean bold) {
        TextView v = new TextView(this);
        v.setText(text);
        v.setTextSize(sizeSp);
        v.setTextColor(color);
        if (bold) v.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return v;
    }

    private void showSetup() {
        webView = null;

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(dp(24), dp(24), dp(24), dp(24));
        root.setBackgroundColor(BG);

        Space top = new Space(this);
        root.addView(top, new LinearLayout.LayoutParams(1, 0, 0.5f));

        ImageView logo = new ImageView(this);
        logo.setImageResource(com.iu2jmz.ft8hunterremote.R.drawable.ft8hunter);
        logo.setScaleType(ImageView.ScaleType.CENTER_INSIDE);
        LinearLayout.LayoutParams logoLp = new LinearLayout.LayoutParams(dp(106), dp(106));
        logoLp.bottomMargin = dp(12);
        root.addView(logo, logoLp);

        TextView title = label("FT8 HUNTER REMOTE", 25, TEXT, true);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView call = label("IU2JMZ  ·  2.0.0", 15, CYAN, true);
        call.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams callLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        callLp.topMargin = dp(4);
        callLp.bottomMargin = dp(24);
        root.addView(call, callLp);

        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(18), dp(18), dp(18), dp(18));
        card.setBackground(rounded(PANEL, Color.rgb(31, 75, 112), 16));
        LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        cardLp.bottomMargin = dp(16);
        root.addView(card, cardLp);

        TextView addressLabel = label("Indirizzo FT8 Hunter sul PC", 13, MUTED, false);
        card.addView(addressLabel);

        EditText address = new EditText(this);
        address.setSingleLine(true);
        address.setTextColor(TEXT);
        address.setHintTextColor(MUTED);
        address.setHint("192.168.1.100:8787");
        address.setText(serverUrl);
        address.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        address.setPadding(dp(12), dp(10), dp(12), dp(10));
        address.setBackground(rounded(Color.rgb(7, 20, 34), Color.rgb(36, 76, 112), 9));
        LinearLayout.LayoutParams editLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        editLp.topMargin = dp(6);
        editLp.bottomMargin = dp(16);
        card.addView(address, editLp);

        TextView tokenLabel = label("Token di accesso", 13, MUTED, false);
        card.addView(tokenLabel);

        EditText tokenEdit = new EditText(this);
        tokenEdit.setSingleLine(true);
        tokenEdit.setTextColor(TEXT);
        tokenEdit.setHintTextColor(MUTED);
        tokenEdit.setHint("Token mostrato in FT8 Hunter Remote sul PC");
        tokenEdit.setText(token);
        tokenEdit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
        tokenEdit.setPadding(dp(12), dp(10), dp(12), dp(10));
        tokenEdit.setBackground(rounded(Color.rgb(7, 20, 34), Color.rgb(36, 76, 112), 9));
        LinearLayout.LayoutParams tokenLp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        tokenLp.topMargin = dp(6);
        tokenLp.bottomMargin = dp(18);
        card.addView(tokenEdit, tokenLp);

        Button connect = new Button(this);
        connect.setAllCaps(false);
        connect.setText("Connetti a FT8 Hunter");
        connect.setTextColor(Color.WHITE);
        connect.setTextSize(16);
        connect.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        connect.setBackground(rounded(Color.rgb(0, 132, 206), CYAN, 10));
        connect.setOnClickListener(v -> {
            String normalized = normalizeUrl(address.getText().toString());
            String t = tokenEdit.getText().toString().trim();
            if (normalized.isEmpty()) {
                Toast.makeText(this, "Inserisci l'indirizzo del PC", Toast.LENGTH_SHORT).show();
                return;
            }
            if (t.isEmpty()) {
                Toast.makeText(this, "Inserisci il token di accesso", Toast.LENGTH_SHORT).show();
                return;
            }

            serverUrl = normalized;
            token = t;
            prefs.edit()
                    .putString(KEY_URL, serverUrl)
                    .putString(KEY_TOKEN, token)
                    .apply();
            showRemote();
        });
        card.addView(connect, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(52)));

        TextView help = label(
                "Sul PC: Tools → FT8 Hunter → FT8 Hunter Remote 2.0.0. " +
                "Attiva l'accesso LAN/VPN, quindi usa qui l'indirizzo e il token mostrati dal programma.",
                12, MUTED, false);
        help.setGravity(Gravity.CENTER);
        help.setLineSpacing(0f, 1.18f);
        root.addView(help, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        Space bottom = new Space(this);
        root.addView(bottom, new LinearLayout.LayoutParams(1, 0, 0.6f));

        setContentView(root);
    }

    private String normalizeUrl(String raw) {
        String value = raw == null ? "" : raw.trim();
        if (value.isEmpty()) return "";
        if (!value.startsWith("http://") && !value.startsWith("https://")) {
            value = "http://" + value;
        }

        try {
            URI uri = new URI(value);
            String scheme = uri.getScheme() == null ? "http" : uri.getScheme();
            String host = uri.getHost();
            int port = uri.getPort();

            if (host == null || host.trim().isEmpty()) {
                return "";
            }
            if (port < 0) port = 8787;

            value = scheme + "://" + host + ":" + port + "/";
        } catch (Exception e) {
            return "";
        }
        return value;
    }

    private void showRemote() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(BG);

        webView = new WebView(this);
        webView.setBackgroundColor(BG);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        settings.setAllowContentAccess(true);
        settings.setAllowFileAccess(false);
        settings.setMediaPlaybackRequiresUserGesture(true);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        settings.setUserAgentString(settings.getUserAgentString() + " FT8HunterRemote/2.0.0");

        webView.setWebChromeClient(new WebChromeClient());
        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView view, String url) {
                super.onPageFinished(view, url);
                String quoted = JSONObject.quote(token);
                String js =
                        "try{" +
                        "localStorage.setItem('ft8hunter-token'," + quoted + ");" +
                        "var t=document.querySelector('#token');if(t)t.value=" + quoted + ";" +
                        "if(typeof connectRemote==='function')connectRemote();" +
                        "}catch(e){}";
                view.evaluateJavascript(js, null);
            }

            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                super.onReceivedError(view, request, error);
                if (request != null && request.isForMainFrame()) {
                    Toast.makeText(MainActivity.this,
                            "PC FT8 Hunter non raggiungibile", Toast.LENGTH_LONG).show();
                }
            }
        });

        root.addView(webView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout tools = new LinearLayout(this);
        tools.setOrientation(LinearLayout.HORIZONTAL);
        tools.setPadding(dp(4), dp(4), dp(4), dp(4));
        tools.setBackground(rounded(Color.argb(220, 8, 21, 34), Color.rgb(36, 81, 116), 14));

        Button reload = miniButton("↻");
        reload.setContentDescription("Ricarica");
        reload.setOnClickListener(v -> {
            if (webView != null) webView.reload();
        });
        tools.addView(reload, new LinearLayout.LayoutParams(dp(48), dp(44)));

        Button setup = miniButton("⚙");
        setup.setContentDescription("Impostazioni connessione");
        setup.setOnClickListener(v -> showSetup());
        LinearLayout.LayoutParams setupLp = new LinearLayout.LayoutParams(dp(48), dp(44));
        setupLp.leftMargin = dp(4);
        tools.addView(setup, setupLp);

        FrameLayout.LayoutParams toolsLp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                Gravity.END | Gravity.BOTTOM);
        toolsLp.setMargins(dp(12), dp(12), dp(12), dp(18));
        root.addView(tools, toolsLp);

        setContentView(root);
        webView.loadUrl(serverUrl);
    }

    private Button miniButton(String text) {
        Button b = new Button(this);
        b.setAllCaps(false);
        b.setText(text);
        b.setTextSize(22);
        b.setTextColor(TEXT);
        b.setPadding(0, 0, 0, 0);
        b.setBackground(rounded(Color.rgb(16, 39, 64), Color.rgb(45, 93, 135), 10));
        return b;
    }

    @Override
    public void onBackPressed() {
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
        } else if (webView != null) {
            showSetup();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.stopLoading();
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }
}
