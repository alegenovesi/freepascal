{
    This file is part of the Free Pascal run time library.
    Copyright (c) 2000 by Florian Klaempfl
    member of the Free Pascal development team.

    This file implements the helper routines for dyn. Arrays in FPC

    See the file COPYING.FPC, included in this distribution,
    for details about the copyright.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

 **********************************************************************
}

type
   { don't add new fields, the size is used }
   { to calculate memory requirements       }
   pdynarray = ^tdynarray;
   tdynarray = packed record
      refcount : ptrint;
      high : tdynarrayindex;
   end;

   pdynarraytypedata = ^tdynarraytypedata;
   tdynarraytypedata =
{$ifndef FPC_REQUIRES_PROPER_ALIGNMENT}
   packed
{$endif FPC_REQUIRES_PROPER_ALIGNMENT}
   record
     elSize : PtrUInt;
     elType2 : Pointer;
     varType : Longint;
   end;

procedure fpc_dynarray_rangecheck(p : pointer;i : tdynarrayindex);[Public,Alias:'FPC_DYNARRAY_RANGECHECK']; compilerproc;
  begin
     if not(assigned(p)) or (i<0) or (i>pdynarray(p-sizeof(tdynarray))^.high) then
       HandleErrorAddrFrameInd(201,get_pc_addr,get_frame);
  end;


function fpc_dynarray_length(p : pointer) : tdynarrayindex;[Public,Alias:'FPC_DYNARRAY_LENGTH']; compilerproc;
  begin
     if assigned(p) then
       fpc_dynarray_length:=pdynarray(p-sizeof(tdynarray))^.high+1
     else
       fpc_dynarray_length:=0;
  end;


function fpc_dynarray_high(p : pointer) : tdynarrayindex;[Public,Alias:'FPC_DYNARRAY_HIGH']; compilerproc;
  begin
     if assigned(p) then
       fpc_dynarray_high:=pdynarray(p-sizeof(tdynarray))^.high
     else
       fpc_dynarray_high:=-1;
  end;


procedure fpc_dynarray_clear(var p : pointer;ti : pointer); [Public,Alias:'FPC_DYNARRAY_CLEAR']; compilerproc;
  var
     realp : pdynarray;
  begin
    if (P=Nil) then
      exit;
    realp:=pdynarray(p-sizeof(tdynarray));
    if realp^.refcount=0 then
      HandleErrorAddrFrameInd(204,get_pc_addr,get_frame);

    if declocked(realp^.refcount) then
      begin
        ti:=aligntoptr(ti+2+PByte(ti)[1]);
        int_finalizearray(p,pdynarraytypedata(ti)^.elType2,realp^.high+1);
        freemem(realp);
      end;
    p:=nil;
  end;

{ alias for internal use }
Procedure fpc_dynarray_clear (var p : pointer;ti : pointer);[external name 'FPC_DYNARRAY_CLEAR'];


procedure fpc_dynarray_incr_ref(p : pointer);[Public,Alias:'FPC_DYNARRAY_INCR_REF']; compilerproc;
  var
     realp : pdynarray;
  begin
     if p=nil then
       exit;

     realp:=pdynarray(p-sizeof(tdynarray));
     if realp^.refcount=0 then
       HandleErrorAddrFrameInd(204,get_pc_addr,get_frame);

     inclocked(realp^.refcount);
  end;

{ provide local access to dynarr_decr_ref for dynarr_setlength }
procedure fpc_dynarray_incr_ref(p : pointer); [external name 'FPC_DYNARRAY_INCR_REF'];


procedure fpc_dynarray_assign(var dest: Pointer; src: Pointer; ti: pointer);[public,alias:'FPC_DYNARRAY_ASSIGN']; compilerproc;
  begin
    fpc_dynarray_incr_ref(src);
    fpc_dynarray_clear(dest,ti);
    Dest:=Src;
  end;

procedure fpc_dynarray_assign(var dest: Pointer; src: Pointer; ti: pointer);[external name 'FPC_DYNARRAY_ASSIGN'];

{ provide local access to dynarr_setlength }
procedure int_dynarray_setlength(var p : pointer;pti : pointer;
  dimcount : sizeint;dims : pdynarrayindex);[external name 'FPC_DYNARR_SETLENGTH'];

procedure fpc_dynarray_setlength(var p : pointer;pti : pointer;
  dimcount : sizeint;dims : pdynarrayindex);[Public,Alias:'FPC_DYNARR_SETLENGTH']; compilerproc;

  var
     i : tdynarrayindex;
     movelen,
     size : sizeint;
     { contains the "fixed" pointers where the refcount }
     { and high are at positive offsets                 }
     realp,newp : pdynarray;
     ti : pointer;
     updatep: boolean;
     elesize : sizeint;
     eletype : pointer;

  begin
     { negative length is not allowed }
     if dims[0]<0 then
       HandleErrorAddrFrameInd(201,get_pc_addr,get_frame);

     { skip kind and name }
     ti:=aligntoptr(Pointer(pti)+2+PByte(pti)[1]);

     elesize:=pdynarraytypedata(ti)^.elSize;
     eletype:=pdynarraytypedata(ti)^.elType2;

     { determine new memory size }
     size:=elesize*dims[0]+sizeof(tdynarray);
     updatep := false;

     { not assigned yet? }
     if not(assigned(p)) then
       begin
          { do we have to allocate memory? }
          if dims[0] = 0 then
            exit;
          getmem(newp,size);
          fillchar(newp^,size,0);
          updatep := true;
       end
     else
       begin
          { if the new dimension is 0, we've to release all data }
          if dims[0]=0 then
            begin
               fpc_dynarray_clear(p,pti);
               exit;
            end;

          realp:=pdynarray(p-sizeof(tdynarray));
          newp := realp;

          if realp^.refcount<>1 then
            begin
               updatep := true;
               { make an unique copy }
               getmem(newp,size);
               fillchar(newp^,size,0);
               if realp^.high < dims[0] then
                 movelen := realp^.high+1
               else
                 movelen := dims[0];
               move(p^,(pointer(newp)+sizeof(tdynarray))^,elesize*movelen);

               { increment ref. count of members }
               for i:= 0 to movelen-1 do
                 int_addref(pointer(newp)+sizeof(tdynarray)+elesize*i,eletype);

               { a declock(ref. count) isn't enough here }
               { it could be that the in MT environments  }
               { in the mean time the refcount was       }
               { decremented                             }

               { it is, because it doesn't really matter }
               { if the array is now removed             }
               fpc_dynarray_clear(p,pti);
            end
          else if dims[0]<>realp^.high+1 then
            begin
               { range checking is quite difficult ...  }
               { if size overflows then it is less than }
               { the values it was calculated from      }
               if (size<sizeof(tdynarray)) or
                 ((elesize>0) and (size<elesize)) then
                 HandleErrorAddrFrameInd(201,get_pc_addr,get_frame);

               { resize? }
               { here, realp^.refcount has to be one, otherwise the previous }
               { if-statement would have been taken. Or is this also for MT  }
               { code? (JM)                                                  }
               if realp^.refcount=1 then
                 begin
                    { shrink the array? }
                    if dims[0]<realp^.high+1 then
                      begin
                          int_finalizearray(pointer(realp)+sizeof(tdynarray)+
                            elesize*dims[0],
                            eletype,realp^.high-dims[0]+1);
                         reallocmem(realp,size);
                      end
                    else if dims[0]>realp^.high+1 then
                      begin
                         reallocmem(realp,size);
                         fillchar((pointer(realp)+sizeof(tdynarray)+elesize*(realp^.high+1))^,
                           (dims[0]-realp^.high-1)*elesize,0);
                      end;
                    newp := realp;
                    updatep := true;
                 end;
            end;
       end;
    { handle nested arrays }
    if dimcount>1 then
      begin
         for i:=0 to dims[0]-1 do
           int_dynarray_setlength(pointer((pointer(newp)+sizeof(tdynarray)+i*elesize)^),
             eletype,dimcount-1,@dims[1]);
      end;
     if updatep then
       begin
         p:=pointer(newp)+sizeof(tdynarray);
         newp^.refcount:=1;
         newp^.high:=dims[0]-1;
       end;
  end;


{ provide local access to dynarr_copy }
function int_dynarray_copy(psrc : pointer;ti : pointer;
    lowidx,count:tdynarrayindex) : fpc_stub_dynarray;[external name 'FPC_DYNARR_COPY'];

function fpc_dynarray_copy(psrc : pointer;ti : pointer;
    lowidx,count:tdynarrayindex) : fpc_stub_dynarray;[Public,Alias:'FPC_DYNARR_COPY'];compilerproc;
  var
    realpsrc : pdynarray;
    i,size : sizeint;
    elesize : sizeint;
    eletype : pointer;
  begin
     fpc_dynarray_clear(pointer(result),ti);
     if psrc=nil then
       exit;
{$ifndef FPC_DYNARRAYCOPY_FIXED}
     if (lowidx=-1) and (count=-1) then
       begin
         lowidx:=0;
         count:=high(tdynarrayindex);
       end;
{$endif FPC_DYNARRAYCOPY_FIXED}
     realpsrc:=pdynarray(psrc-sizeof(tdynarray));
     if (lowidx<0) then
       begin
       { Decrease count if index is negative, this is different from how copy()
         works on strings. Checked against D7. }
         if count<=0 then
           exit;              { may overflow when adding lowidx }
         count:=count+lowidx;
         lowidx:=0;
       end;
     if (count>realpsrc^.high-lowidx+1) then
       count:=realpsrc^.high-lowidx+1;
     if count<=0 then
       exit;

     { skip kind and name }
     ti:=aligntoptr(ti+2+PByte(ti)[1]);

     elesize:=pdynarraytypedata(ti)^.elSize;
     eletype:=pdynarraytypedata(ti)^.elType2;

     { create new array }
     size:=elesize*count;
     getmem(pointer(result),size+sizeof(tdynarray));
     pdynarray(result)^.refcount:=1;
     pdynarray(result)^.high:=count-1;
     inc(pointer(result),sizeof(tdynarray));
     { copy data }
     move(pointer(psrc+elesize*lowidx)^,pointer(result)^,size);

     { increment ref. count of members? }
     if PByte(eletype)^ in tkManagedTypes then
       for i:=0 to count-1 do
         int_addref(pointer(pointer(result)+elesize*i),eletype);
  end;


procedure DynArraySetLength(var a: Pointer; typeInfo: Pointer; dimCnt: SizeInt; lengthVec: PSizeInt);
  external name 'FPC_DYNARR_SETLENGTH';

{ obsolete but needed for bootstrapping }
procedure fpc_dynarray_decr_ref(var p : pointer;ti : pointer); [Public,Alias:'FPC_DYNARRAY_DECR_REF']; compilerproc;
  begin
    fpc_dynarray_clear(p,ti);
  end;

