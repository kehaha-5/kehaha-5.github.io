$(document).ready(function () {
    wrapImageWithFancyBox();
});

/**
 * Wrap images with fancybox support.
 * edit from https://tianma8023.github.io/post/hexo-material-intergrate-image-display-feature/
 */
function wrapImageWithFancyBox() {
    $('img').each(function () {
        let image = $(this);
        //头像
        if (image.parent("div") && image.parent("div").hasClass("avatar")) {
            return;
        }
        let html = `<a data-fancybox="images" href=${image.attr("src")}></a>`;
        image.wrap(html);
    });

    $().fancybox({
        selector: '[data-fancybox="images"]',
        thumbs: false,
        hash: true,
        loop: false,
        fullScreen: false,
        slideShow: false,
        protect: true,
    });
}