
open Lwt.Infix;

module String = {

  module Kv = {

    module Store = Ezirmin.FS_lww_register Irmin.Contents.String;

    let create ::file =>
    Store.init root::file bare::true () >>= Store.master;

    let write branch k v =>
      branch >>= fun branch' =>
        Store.write message::"write_kv" branch' path::["kv", k] v >>=
          fun _ => Lwt.return_unit;

    let read branch k =>
      branch >>= fun branch' =>
        Store.read branch' path::["kv", k] >>=
          fun data =>
            switch data {
            | Some json => Lwt.return json;
            | None => Lwt.return "";
          };
  };

};

module Json = {

  let json_empty = Ezjsonm.dict [];

  module Kv = {

    module Store = Ezirmin.FS_lww_register Irmin.Contents.Json;

    let create ::file =>
      Store.init root::file bare::true () >>= Store.master;
  
    let write branch k v => {
      open Printf;
      branch >>= fun branch' =>
        Store.write message::(sprintf "write_kv (%s)\n" k) branch' path::["kv", k] v >>=
          fun _ => Lwt.return_unit;
    };

    let read branch k =>
      branch >>= fun branch' =>
        Store.read branch' path::["kv", k] >>=
          fun data =>
            switch data {
            | Some json => Lwt.return json;
            | None => Lwt.return json_empty;
          };  
  };

  module Ts = {

    module Store = Ezirmin.FS_log (Tc.Pair Tc.Int Irmin.Contents.Json);

    let get_time () => {
      let t_sec = Unix.gettimeofday ();
      let t_ms = t_sec *. 1000.0;
      int_of_float t_ms;
    };

    let without_timestamp l =>
      List.map (fun (_, json) => Ezjsonm.value json) l |>
        fun l => Ezjsonm.(`A l);
      
    let with_timestamp l => {
      open Ezjsonm;
        List.map (fun (t,json) => dict [("timestamp", int t), ("data", value json)]) l |>
          fun l => `A l;
    };

    let since t l => List.filter (fun (ts, _) => ts >= t) l;
    
    let until t l => List.filter (fun (ts, _) => ts <= t) l;
    
    let range t1 t2 l => since t1 l |> until t2;   

    let create ::file =>
      Store.init root::file bare::true () >>= Store.master;

    let write branch ts id v => {
      open Printf;
      branch >>=
        fun branch' => {
          switch ts {
          | Some t => Store.append message::(sprintf "write_ts (%s)\n" id) branch' path::["ts", id] (t, v)
          | None => Store.append message::(sprintf "write_ts (%s)\n" id) branch' path::["ts", id] (get_time (), v)
          };
        };
      };

    module Simple = {

      let get_cursor branch id =>
        branch >>= (fun branch' => Store.get_cursor branch' path::["ts", id]);

      let read_from_cursor cursor n =>
        switch cursor {
        | Some c => Store.read c n
        | None => Lwt.return ([], cursor)
        }; /* returns dataset with cursor */

      let read branch id n =>
        branch >>=
          fun branch' =>
            Store.get_cursor branch' path::["ts", id] >>=
              fun cursor => read_from_cursor cursor n;     
              
              
      let read_latest branch id => {
        open Ezjsonm;
        read branch id 1 >>=
          fun (data, _) =>
            switch data {
            | [] => Lwt.return json_empty;
            | [(t,json), ..._] => Lwt.return (dict [("timestamp", int t), ("data", value json)]);
            };
      };         

    };

    module Complex = {
          
      let read_data_all branch id =>
        branch >>=
          /* might need to look at paging this back for large sets */
          fun branch' => Store.read_all branch' path::["ts", id];

      let take n xs => {
        open List;
        let rec take' n xs acc =>
          switch n {
          | 0 => rev acc
          | _ => take' (n - 1) (tl xs) [hd xs, ...acc]
          };
        take' n xs []
      };
      
      
      let order f n l => {
        open List;
        if (n > 0) {
          let n' = min n (length l);
          let l' = sort f l;
          take n' l';
        } else [];  
      };

      let last n l => {
        let f (ts,_) (ts',_) => (ts < ts') ? 1 : -1;
        order f n l; 
      };

      let first n l => {
        let f (ts,_) (ts',_) => (ts > ts') ? 1 : -1;
        order f n l; 
      };
          
      let read_last branch id n =>
        read_data_all branch id >>=
          fun l => Lwt.return (with_timestamp (last n l));

      let read_first branch id n =>
        read_data_all branch id >>=
          fun l => Lwt.return (with_timestamp (first n l));        

      let car json => {
          open Ezjsonm;
          switch json {
          | `A [] => json_empty;
          | `A [item, ...rest] => item;
          } |> Lwt.return;
      };    
          
      let read_latest branch id =>
        read_last branch id 1 >>= car;

      let read_earliest branch id =>
        read_first branch id 1 >>= car;
          
      let read_all branch id =>
        read_data_all branch id >>=
          fun data => Lwt.return (with_timestamp data);

      let sort_data l => {
        let f (ts,_) (ts',_) => (ts < ts') ? 1 : -1;
        l |> List.sort f |> with_timestamp |> Lwt.return;
      };

      let read_since branch id t =>
        read_data_all branch id >>=
          fun l => since t l |> sort_data;

      let read_range branch id t1 t2 =>
        read_data_all branch id >>=
          fun l => range t1 t2 l |> sort_data;  
        
    };
              
  };    

};


