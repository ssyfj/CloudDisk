function dealfile(target_url,send_data){
    $.ajax({
        type : 'post',
        url : target_url, 
        cache : false,
        data : send_data,  
        dataType : 'json', 
        success : function(data){
          alert(data["token"]);
          document.location.reload();
        },
        error : function(){ 
          alert("fail to deal file!");
        }
    });
}

function dealdownCnt(target_url,send_data){
    $.ajax({
        type : 'post',
        url : target_url, 
        cache : false,
        data : send_data,  
        dataType : 'json', 
        success : function(data){
          console.log(data);
        },
        error : function(){ 
          alert("fail to deal file!");
        }
    });
}

function dealdownloadfile(target_url,send_data,filename){
    $.ajax({
        type : 'post',
        url : target_url, 
        cache : false,
        data : send_data,  
        dataType : 'json', 
        success : function(data){
          console.log(data);
          if(data["code"] === "110"){
            var target_url =  GetPostUrlPath("dealfile.jsp?cmd=pv");
            dealdownCnt(target_url,send_data);

            var a = document.createElement("a");    //模拟链接，进行点击下载
            a.href = data["token"];
            a.style.display = "none";    //不显示
            a.download = filename;
            a.click();
          }
        },
        error : function(){ 
          alert("fail to deal file!");
        }
    });
}


$(document).ready(function(){
    var menu = new BootstrapMenu('.filemember', {
      fetchElementData: function($rowElem) {
        var filename = $rowElem.find("p").text();
        var filemd5 = $rowElem.find(".member-image").attr("id")
        var enable = false;
        if (filename != null && filemd5 != null){
          enable = true;
        }
        var row = {
            isEditable:enable,
            "filename":filename,
            "filemd5":filemd5
        }
        return row;
      },
      /* group actions by their id to make use of separators between
       * them in the context menu. Actions not added to any group with
       * this option will appear in a default group of their own. */
      actionsGroups: [
        ['downloadFile', 'shareFile'],
        ['genKey'],
        ['deleteFile']
      ],
      /* you can declare 'actions' as an object instead of an array,
       * and its keys will be used as action ids. */
      actions: {
        downloadFile: {
          name: 'Download File',
          iconClass: 'fa-download',
          onClick: function(row) {
            var data = JSON.stringify({
                "user":$.cookie("get",{name:"user"}),
                "token" : $.cookie("get",{name:"token"}),
                "md5" : row.filemd5,
                "filename" : row.filename
            });
            var target_url =  GetPostUrlPath("download.jsp?cmd=webdownload");
            dealdownloadfile(target_url,data,row.filename);
          },
          isEnabled: function(row) {
            return true;
          }
        },
        shareFile: {
          name: 'Share File',
          iconClass: 'fa-share',
          onClick: function(row) {
            var data = JSON.stringify({
                "user":$.cookie("get",{name:"user"}),
                "token" : $.cookie("get",{name:"token"}),
                "md5" : row.filemd5,
                "filename" : row.filename
            });
            var target_url =  GetPostUrlPath("dealfile.jsp?cmd=share");
            dealfile(target_url,data);
          },
          isEnabled: function(row) {
            return true;
          }
        },
        genKey: {
          name: 'Gene ExtCode',
          iconClass: 'fa-key',
          onClick: function(row) {
            var data = JSON.stringify({
                "user":$.cookie("get",{name:"user"}),
                "token" : $.cookie("get",{name:"token"}),
                "md5" : row.filemd5,
                "filename" : row.filename
            });
            var target_url =  GetPostUrlPath("dealfile.jsp?cmd=gen");
            dealfile(target_url,data);
          },
          isShown: function(row) {
            return true;
          }
        },
        deleteFile: {
          name: 'Delete File',
          iconClass: 'fa-trash-o',
          onClick: function(row) {
            var data = JSON.stringify({
                "user":$.cookie("get",{name:"user"}),
                "token" : $.cookie("get",{name:"token"}),
                "md5" : row.filemd5,
                "filename" : row.filename
            });
            var target_url =  GetPostUrlPath("dealfile.jsp?cmd=del");
            dealfile(target_url,data);
          },
          isShown: function(row) {
            return true;
          }
        }
      }
    })
})
