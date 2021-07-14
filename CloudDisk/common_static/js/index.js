$("#subCode").click(function(){
    var code = $("#code").val();
    if(code == "" || code.length != 8){
        alert("format error!\n");
        return;
    }

    var targetUrl = GetPostUrlPath("download.jsp?cmd=code");

    var data = JSON.stringify({
                    "user": $.cookie("get",{name:"user"}),
                    "token": $.cookie("get",{name:"token"}),
                    "code": code,
                });

    $.ajax({
        type : 'post',
        url : targetUrl, 
        cache : false,
        data : data,  
        dataType : 'json', 
        success : function(data){
            console.log(data);
            if(data["code"] == "110"){
                var a = document.createElement("a");    //模拟链接，进行点击下载
                a.href = data["token"];
                a.style.display = "none";    //不显示
                a.click();
            }
        },
        error : function(){ 
            alert("请求失败")
        }
    });
});

