from django.shortcuts import render,HttpResponse
from django_redis import get_redis_connection
from django.shortcuts import redirect
from django.core.paginator import Paginator,PageNotAnInteger,EmptyPage
from functools import wraps
from fdfs import models
from django.db.models import Sum,Count
import json

def login_auth(func):
    @wraps(func)
    def inner(request,*args,**kwargs):
        #获取当前路径
        cur_path = request.get_full_path()
        #校验session,先获取cookie中的数据
        cookie_user = request.COOKIES.get("user",None)
        cookie_token = request.COOKIES.get("token",None)
        if cookie_user:
            if request.session.get(cookie_user,None) == cookie_token:
                return func(request,*args,**kwargs)
            conn = get_redis_connection("default")
            redis_token = conn.get(cookie_user)
            if redis_token == bytes(cookie_token,encoding="utf-8"):
                request.session[cookie_user] = cookie_token
                request.session.set_expiry(86400)
                return func(request,*args,**kwargs)
        return redirect("/fdfs/login.html?next=%s"%cur_path)
    return inner
	    
	    
# Create your views here.
def index(req):
    cookie_user = req.COOKIES.get("user",None)
    cookie_token = req.COOKIES.get("token",None)
    print(cookie_user,cookie_token)
    if cookie_user == None:
        return render(req,"index.html")
    conn = get_redis_connection("default")
    redis_token = conn.get(cookie_user)
    context = {"user":"null"}
    if req.session.get(cookie_user,None) != None:
        context["user"] = cookie_user
    print("redis token:",redis_token,context)
    return render(req,"index.html",context)

def login(req):
    return render(req,"login.html")

def getStaticData(userInfo):
    fileCount = models.UserFileCount.objects.filter(user=userInfo).values("count");
    userCount = models.UserInfo.objects.count();
    shareCount = models.ShareFileList.objects.count();
    downCount = models.UserFileList.objects.filter(user=userInfo).aggregate(Sum("pv"))
    if fileCount.exists() == False:
        fileCount = 0
    else:
        fileCount = fileCount[0]["count"]

    if downCount["pv__sum"] == None:
        downCount = 0
    else:
        downCount = downCount["pv__sum"]

    return {"fileCnt":fileCount,"userCnt":userCount,"shareCnt":shareCount,"downCnt":downCount}

@login_auth
def file(req):
    cookie_user = req.COOKIES.get("user",None)
    context = {"user":cookie_user}
    context["static"] = getStaticData(cookie_user)
    
    file_list = models.UserFileList.objects.values().filter(user=cookie_user).order_by("-id");
    paginator = Paginator(file_list,18)
    page = req.GET.get("p",1);
    try:
        pData = paginator.page(page)
    except PageNotAnInteger:
        pData = paginator.page(1)
    except EmptyPage:
        pData = paginator.page(paginator.num_pages)

    context["posts"] = pData   
    return render(req,"myfile.html",context)

@login_auth
def share(req):
    cookie_user = req.COOKIES.get("user",None)
    context = {"user":cookie_user}
    context["static"] = getStaticData(cookie_user)
    file_list = models.ShareFileList.objects.values().order_by("-id");
    paginator = Paginator(file_list,18)
    page = req.GET.get("p",1);
    try:
        pData = paginator.page(page)
    except PageNotAnInteger:
        pData = paginator.page(1)
    except EmptyPage:
        pData = paginator.page(paginator.num_pages)

    context["posts"] = pData
    return render(req,"sharefile.html",context)

@login_auth
def upload(req):
    cookie_user = req.COOKIES.get("user",None)
    context = {"user":cookie_user}
    context["static"] = getStaticData(cookie_user)
    return render(req,"uploadfile.html",context)

@login_auth
def logout(req):
    if req.method =='POST':
        print(req.body)
        json_res = json.loads(req.body);
        if req.session.get(json_res["user"]) == json_res["token"]:
            req.session.flush();
            conn = get_redis_connection("default")
            #print(conn.__dict__)
            conn.delete(json_res["user"])
            return HttpResponse(json.dumps({"code":"0","message":"success"}), content_type="application/json")
        else:
            return HttpResponse(json.dumps({"code":"1","message":"failed"}), content_type="application/json")
    else:
        return HttpResponse("<h1>404</h1>")
