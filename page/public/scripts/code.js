$(document).ready(function() {
    fix_logo_height();
    
    var page = getUrlParameter("page");
    if(page == undefined) {
        page = "domov";
    }

    load_page("/pages/" + page + ".html");
});

var refresh_interval = null; // gets set in slika.html

/**
 * load_page - Počisti interval iz strani slika.html in naloži stran page.
 *
 * @param  {string} page   Stran, ki naj se naloži
 */
function load_page(page) {
    clearInterval(refresh_interval);

    //$(".sp-hidden").remove(); // Fix za brisanje color pickerja pri prestavljanju strani
	//Fix 2: v spectrum.js nastaviš da se stvar appenda nekemu elementu na podstrani 'lucke', npr. #cp-right

    $.get(page)
        .done(function(data) {
            $("#main-container").html(data);
            set_language(current_language);

            $("#main-navigation li a").each(function(idx) {
                if($(this).attr("onclick").indexOf(page) != -1) {
                    $(this).parent().addClass("active");
                } else {
                    $(this).parent().removeClass("active");
                }
            });
        })
        .fail(function(error) {
            console.log(error);
        });
}

var current_language = 'si';
var languages = ['si', 'en'];

/**
 * set_language - Nastavi jezik glede na podan argument.
 * V dropdown-u nastavi kljukico, skrije vse .lang div-e in prikaze
 * samo tiste, ki so potrebni.
 *
 * @param  {string} lang Izbrani jezik
 */
function set_language(lang) {
    current_language = lang;

    // hide glyphicons
    $("#language-dropdown li a span").each(function(idx) {
        if($(this).data("lang") !== current_language) {
            $(this).hide();
        } else {
            $(this).show();
        }
    });

    // show correct language
    $(".lang").hide();
    $("." + current_language).show();
}

function fix_logo_height() {
    $("#img-logo").height($("#img-logo").width());
}
$(window).resize(fix_logo_height);

/**
 * getUrlParameter - Vrne GET parameter naložene strani.
 *
 * @param  {string} sParam Ime parametra
 * @return {strng}        Vrednost GET parametra
 */
function getUrlParameter(sParam) {
    var sPageURL = decodeURIComponent(window.location.search.substring(1)),
        sURLVariables = sPageURL.split('&'),
        sParameterName,
        i;

    for (i = 0; i < sURLVariables.length; i++) {
        sParameterName = sURLVariables[i].split('=');

        if (sParameterName[0] === sParam) {
            return sParameterName[1] === undefined ? true : sParameterName[1];
        }
    }
};
