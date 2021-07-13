function GetPostUrlPath(addr){
	var url = document.location.toString().split("?")[0];
	var arr = url.split("/");
	var responeUrl = "";
	for(var i = 0; i<arr.length-1; i++){
	    responeUrl += arr[i]+"/";
	}
	responeUrl += addr;
	return responeUrl;
}

function GetRedirectNextUrl(){
	var url = document.location.origin;
	if(document.location.search == null || document.location.search.split("=").length != 2){
		return GetPostUrlPath("index.html");
	}
	var nextTop = document.location.search.split("=")[1];
	return url+nextTop;
}

function logout()
{
	var data = JSON.stringify({
			        "user":$.cookie("get",{name:"user"}),
			        "token" : $.cookie("get",{name:"token"})
                });

    var targetUrl = GetPostUrlPath("logout.jsp");
    $.ajax({
        type : 'post',
        url : targetUrl, 
        cache : false,
        data : data,  
        dataType : 'json', 
        success : function(data){
        	//开始解析返回码
        	if(data["code"] == "0"){	//退出成功
        		window.location.href=GetPostUrlPath("index.html");
        	}else{						//登录失败
        		alert("退出失败");
        	}
        },
        error : function(){ 
			alert("请求失败")
        }
    });
}