var filemd5 = null;

var sleep = function(time) {
    var startTime = new Date().getTime() + parseInt(time, 10);
    while(new Date().getTime() < startTime) {}
};

function init() {
    document.getElementById('file-0').addEventListener('change', function () {
        var blobSlice = File.prototype.slice || File.prototype.mozSlice || File.prototype.webkitSlice,
            file = this.files[0],
            chunkSize = 2097152,                             // Read in chunks of 2MB
            chunks = Math.ceil(file.size / chunkSize),
            currentChunk = 0,
            spark = new SparkMD5.ArrayBuffer(),
            fileReader = new FileReader();

        fileReader.onload = function (e) {
            spark.append(e.target.result);                   // Append array buffer
            currentChunk++;

            if (currentChunk < chunks) {
                loadNext();
            } else {
                filemd5 = spark.end();
            }
        };

        function loadNext() {
            var start = currentChunk * chunkSize,
                end = ((start + chunkSize) >= file.size) ? file.size : start + chunkSize;

            fileReader.readAsArrayBuffer(blobSlice.call(file, start, end));
        }

        loadNext();
    });
}

$("#file-0").fileinput({
    theme: 'fas',
    uploadUrl: GetPostUrlPath("uploadfile.jsp"),
    uploadExtraData: {
        "md5" : filemd5,
        "user":$.cookie("get",{name:"user"}),
        "token" : $.cookie("get",{name:"token"})
    },
    allowedFileExtensions: ['jpg', 'png', 'gif','jpeg',
                            "pdf", "doc", "docx", "txt", "xls","ppt","pptx",
                            "mp3", "mp4", "yuv", "h264",
                            "htm", "html","css", "js",
                            "zip", "gz"],
    overwriteInitial: false,
    initialPreview: false,
    initialPreviewConfig: false,
    initialPreviewThumbTags: false,
    slugCallback: function (filename) {
        if(filemd5 == null){
            sleep(500);
        }
        this.uploadExtraData["md5"] = filemd5;
        return filename;
    }
}).on("fileuploaded", function (event, data, previewId, index) {
    alert("upload success");
    window.location.href=GetPostUrlPath("myfile.html");
}).on('fileuploaderror', function (event, data, msg) {//异步上传失败结果处理
});
